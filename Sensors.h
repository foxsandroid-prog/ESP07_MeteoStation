#pragma once
#include "Config.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include <DHT.h>
#include <Adafruit_BMP085.h>
#include <Adafruit_BME280.h>
#include <BH1750.h>
#include <RCSwitch.h>
#include <Servo.h>

OneWire oneWire(pinOW);
DallasTemperature owSensorsBus(&oneWire);

Adafruit_BMP085 bmp;
Adafruit_BME280 bme;
BH1750 lightMeter;

RCSwitch rfReceiver = RCSwitch();
Servo auxServo;

enum PollPhase : uint8_t {
  POLL_IDLE = 0,
  POLL_OW_WAIT,
  POLL_DHT_STEP,
  POLL_I2C_STEP
};

PollPhase pollPhase = POLL_IDLE;
unsigned long lastPollCycleStart = 0;
unsigned long owConvertStart = 0;
uint8_t stepIndex = 0;

// ================= ИНИЦИАЛИЗАЦИЯ ПЕРИФЕРИИ =================
void initPeripherals() {
  owSensorsBus.begin();
  rfReceiver.enableReceive(digitalPinToInterrupt(pinRF));

  auxServo.attach(pinServo);
  auxServo.write(constrain(cfg.servoAngle, 0, 180));

  for (uint8_t i = 0; i < MAX_I2C_DEVICES; i++) {
    if (cfg.i2cDevices[i].fmt.enabled && cfg.i2cDevices[i].type == I2C_PCF8574) {
      applyPcfOutputs(i);
    }
  }
}

void setServoAngle(uint8_t angle) {
  cfg.servoAngle = constrain(angle, 0, 180);
  auxServo.write(cfg.servoAngle);
  saveConfig();
}

// ================= КОНТРОЛЬ АКБ 18650 И ПИТАНИЯ 24В =================
void updatePowerStatus() {
  // Делитель 330 кОм (R1) / 100 кОм (R2) -> коэффициент ~4.30
  int raw = analogRead(pinADC);
  float vAdc = (raw / 1023.0) * 1.0; // Опорное напряжение ADC на голом ESP-07 = 1.0 В
  batteryVoltage = vAdc * 4.30;

  // Контроль наличия 24В через вход PCF8574 (если настроен)
  if (cfg.powerSensePcfIdx < MAX_I2C_DEVICES &&
      cfg.i2cDevices[cfg.powerSensePcfIdx].type == I2C_PCF8574 &&
      i2cValues[cfg.powerSensePcfIdx].online) {
    uint8_t portState = (uint8_t)i2cValues[cfg.powerSensePcfIdx].val1;
    externalPower24V = (portState >> cfg.powerSensePcfBit) & 1;
  } else {
    externalPower24V = true; // По умолчанию считаем, что сеть есть
  }
}

// ================= ПРИЁМНИК ОХРАННЫХ ДАТЧИКОВ 433 МГц =================
void checkRfReceiver() {
  if (rfReceiver.available()) {
    unsigned long code = rfReceiver.getReceivedValue();
    if (code != 0) {
      lastRfCode = code;
      lastRfTime = millis();
      // Здесь можно добавить реакцию на конкретный код датчика, например:
      // if (code == 1234567) { ... }
    }
    rfReceiver.resetAvailable();
  }
}

// ================= ОПРОС ДАТЧИКОВ =================
void startOneWireConversion() {
  owSensorsBus.setWaitForConversion(false);
  owSensorsBus.requestTemperatures();
  owConvertStart = millis();
  pollPhase = POLL_OW_WAIT;
}

void finishOneWireConversion() {
  for (uint8_t i = 0; i < MAX_OW_SENSORS; i++) {
    if (cfg.owSensors[i].fmt.enabled && cfg.owSensors[i].addr[0] != 0) {
      float t = owSensorsBus.getTempC(cfg.owSensors[i].addr);
      owValues[i] = (t == DEVICE_DISCONNECTED_C) ? NAN : t;
    }
  }
}

void readSingleDht(uint8_t idx) {
  if (!cfg.dhtSensors[idx].fmt.enabled || cfg.dhtSensors[idx].pin == 0) return;
  DHT dht(cfg.dhtSensors[idx].pin, cfg.dhtSensors[idx].type == 21 ? DHT21 : DHT22);
  dht.begin();
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t)) dhtTemp[idx] = t;
  if (!isnan(h)) dhtHum[idx] = h;
}

void readSingleI2c(uint8_t idx) {
  if (!cfg.i2cDevices[idx].fmt.enabled || cfg.i2cDevices[idx].addr == 0 ||
      cfg.i2cDevices[idx].addr == LCD_ADDR || cfg.i2cDevices[idx].addr == RTC_ADDR) {
    i2cValues[idx].online = false;
    return;
  }
  uint8_t addr = cfg.i2cDevices[idx].addr;
  Wire.beginTransmission(addr);
  if (Wire.endTransmission() != 0) {
    i2cValues[idx].online = false;
    return;
  }
  i2cValues[idx].online = true;

  switch (cfg.i2cDevices[idx].type) {
    case I2C_BMP180_085:
      if (bmp.begin(BMP085_STANDARD)) {
        i2cValues[idx].val1 = bmp.readTemperature();
        i2cValues[idx].val2 = bmp.readPressure() / 133.322;
      }
      break;
    case I2C_BME280:
      if (bme.begin(addr)) {
        i2cValues[idx].val1 = bme.readTemperature();
        i2cValues[idx].val2 = bme.readPressure() / 133.322;
        i2cValues[idx].val3 = bme.readHumidity();
      }
      break;
    case I2C_BH1750:
      if (lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, addr)) {
        i2cValues[idx].val1 = lightMeter.readLightLevel();
      }
      break;
    case I2C_PCF8574:
      applyPcfOutputs(idx);
      Wire.requestFrom(addr, (uint8_t)1);
      if (Wire.available()) {
        i2cValues[idx].val1 = Wire.read();
      }
      break;
    default:
      break;
  }
}

void runBlockingPollAll() {
  owSensorsBus.setWaitForConversion(true);
  owSensorsBus.requestTemperatures();
  finishOneWireConversion();
  for (uint8_t i = 0; i < MAX_DHT_SENSORS; i++) readSingleDht(i);
  for (uint8_t i = 0; i < MAX_I2C_DEVICES; i++) readSingleI2c(i);
  updatePowerStatus();
}

// ================= ДВИЖОК АВТОМАТИЗАЦИИ (ПРАВИЛА ДЛЯ РЕЛЕ) =================
float getSensorParamByName(const char* name, RuleParam param, bool &valid) {
  valid = false;
  if (strcasecmp(name, "BAT") == 0) { valid = true; return batteryVoltage; }
  if (strcasecmp(name, "RF") == 0)  { valid = (lastRfCode != 0); return (float)lastRfCode; }

  // 1-Wire
  for (uint8_t i = 0; i < MAX_OW_SENSORS; i++) {
    if (cfg.owSensors[i].fmt.enabled && strcasecmp(cfg.owSensors[i].fmt.name, name) == 0) {
      if (!isnan(owValues[i])) { valid = true; return owValues[i]; }
      return 0;
    }
  }
  // DHT
  for (uint8_t i = 0; i < MAX_DHT_SENSORS; i++) {
    if (cfg.dhtSensors[i].fmt.enabled && strcasecmp(cfg.dhtSensors[i].fmt.name, name) == 0) {
      float v = (param == PARAM_HUM) ? dhtHum[i] : dhtTemp[i];
      if (!isnan(v)) { valid = true; return v; }
      return 0;
    }
  }
  // I2C
  for (uint8_t i = 0; i < MAX_I2C_DEVICES; i++) {
    if (cfg.i2cDevices[i].fmt.enabled && i2cValues[i].online &&
        strcasecmp(cfg.i2cDevices[i].fmt.name, name) == 0) {
      valid = true;
      if (param == PARAM_PRESS) return i2cValues[i].val2;
      if (param == PARAM_HUM)   return i2cValues[i].val3;
      return i2cValues[i].val1; // Temp или Lux
    }
  }
  return 0;
}

void evaluateAutomationRules() {
  bool changed[MAX_I2C_DEVICES] = {false};

  for (uint8_t r = 0; r < MAX_RULES; r++) {
    AutoRule &rule = cfg.rules[r];
    if (!rule.enabled) continue;
    if (rule.pcfIdx >= MAX_I2C_DEVICES || cfg.i2cDevices[rule.pcfIdx].type != I2C_PCF8574) continue;

    bool valid = false;
    float val = getSensorParamByName(rule.sensorName, rule.param, valid);
    if (!valid) continue;

    bool condMet = false;
    switch (rule.op) {
      case OP_GT:  condMet = (val > rule.threshold); break;
      case OP_LT:  condMet = (val < rule.threshold); break;
      case OP_GTE: condMet = (val >= rule.threshold); break;
      case OP_LTE: condMet = (val <= rule.threshold); break;
      case OP_EQ:  condMet = (fabs(val - rule.threshold) < 0.05); break;
    }

    bool currentBit = (cfg.i2cDevices[rule.pcfIdx].pcfOutMask >> rule.pcfBit) & 1;
    bool desiredBit = currentBit;

    if (condMet) {
      desiredBit = rule.targetState;
    } else if (rule.invertElse) {
      // Небольшой гистерезис 0.2, чтобы реле не дребезжало на границе порога
      if (rule.op == OP_GT || rule.op == OP_GTE) {
        if (val < (rule.threshold - 0.2)) desiredBit = !rule.targetState;
      } else if (rule.op == OP_LT || rule.op == OP_LTE) {
        if (val > (rule.threshold + 0.2)) desiredBit = !rule.targetState;
      } else {
        desiredBit = !rule.targetState;
      }
    }

    if (desiredBit != currentBit) {
      if (desiredBit) cfg.i2cDevices[rule.pcfIdx].pcfOutMask |= (1 << rule.pcfBit);
      else cfg.i2cDevices[rule.pcfIdx].pcfOutMask &= ~(1 << rule.pcfBit);
      changed[rule.pcfIdx] = true;
    }
  }

  for (uint8_t i = 0; i < MAX_I2C_DEVICES; i++) {
    if (changed[i]) applyPcfOutputs(i);
  }
}
