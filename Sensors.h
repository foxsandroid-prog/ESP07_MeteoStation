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
#include <Adafruit_AHTX0.h>
#include <Adafruit_SHT4x.h>
#include <Adafruit_BMP280.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>

String pendingWebhookUrl = "";
unsigned long lastRfProcessedCode = 0;
unsigned long lastRfProcessedTime = 0;


OneWire oneWire(pinOW);
DallasTemperature owSensorsBus(&oneWire);

Adafruit_BMP085 bmp;
Adafruit_BME280 bme;
Adafruit_AHTX0 aht;
Adafruit_SHT4x sht4 = Adafruit_SHT4x();
Adafruit_BMP280 bmp280;
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
  pinMode(pinRF, INPUT);
  rfReceiver.enableReceive(digitalPinToInterrupt(pinRF));
  rfReceiver.setReceiveTolerance(80); // Расширяем допуск по длительности импульсов с 60% до 80%

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
  // Реальные измеренные сопротивления делителя (в кОм)
  const float R1_KOHM = 210.0 + 101.0; // Верхнее плечо (311 кОм)
  const float R2_KOHM = 98.0;          // Нижнее плечо на GND (98 кОм)
  const float ADC_VREF = 1.13;        // Откалибровано по мультиметру (3.20 В / 2.80 В)

  int rawAdc = analogRead(A0);
  float vAdc = (rawAdc / 1023.0) * ADC_VREF;
  batteryVoltage = vAdc * ((R1_KOHM + R2_KOHM) / R2_KOHM);

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
  if (!rfReceiver.available()) return;

  unsigned long code = rfReceiver.getReceivedValue();
  uint16_t bits = rfReceiver.getReceivedBitlength();
  uint8_t proto = rfReceiver.getReceivedProtocol();
  rfReceiver.resetAvailable();

  if (code == 0) return;

  // Антидребезг: один и тот же брелок шлёт пакет 4-6 раз подряд за 0.5 сек
  if (code == lastRfProcessedCode && (millis() - lastRfProcessedTime) < 1500) {
    return;
  }
  lastRfProcessedCode = code;
  lastRfProcessedTime = millis();

  lastRfCode = code;
  lastRfTime = millis();

  // 1. Сдвигаем журнал (храним 5 последних записей)
  for (int i = MAX_RF_LOG - 1; i > 0; i--) {
    rfLog[i] = rfLog[i - 1];
  }
  rfLog[0].code = code;
  rfLog[0].bitLen = bits;
  rfLog[0].proto = proto;
  String tNow = getFormattedTime(); // Берем текущее время "HH:MM:SS"
  strlcpy(rfLog[0].timeStr, tNow.c_str(), sizeof(rfLog[0].timeStr));
  hasUnreadRf = true;

  // 2. Проверяем правила реакции на этот код
  String matchedName = "RF_" + String(code);
  bool needWebhook = false;

  for (uint8_t r = 0; r < MAX_RF_RULES; r++) {
    RfActionRule &rule = cfg.rfRules[r];
    if (!rule.enabled || rule.code != code) continue;

    if (strlen(rule.label) > 0) matchedName = String(rule.label);

    // Управление выходом PCF8574
    if (rule.pcfIdx < MAX_I2C_DEVICES &&
        cfg.i2cDevices[rule.pcfIdx].type == I2C_PCF8574 &&
        rule.relayAction > 0) {
      bool curBit = (cfg.i2cDevices[rule.pcfIdx].pcfOutMask >> rule.pcfBit) & 1;
      bool nextBit = curBit;
      if (rule.relayAction == 1) nextBit = true;
      else if (rule.relayAction == 2) nextBit = false;
      else if (rule.relayAction == 3) nextBit = !curBit;

      if (nextBit != curBit) {
        if (nextBit) cfg.i2cDevices[rule.pcfIdx].pcfOutMask |= (1 << rule.pcfBit);
        else cfg.i2cDevices[rule.pcfIdx].pcfOutMask &= ~(1 << rule.pcfBit);
        applyPcfOutputs(rule.pcfIdx);
      }
    }

    if (rule.sendWebhook) needWebhook = true;
  }

  // 3. Формируем URL для отправки наружу
  if (needWebhook && strlen(cfg.rfWebhookUrl) > 5 && WiFi.status() == WL_CONNECTED) {
    String url = String(cfg.rfWebhookUrl);
    url.replace("%CODE%", String(code));
    url.replace("%NAME%", matchedName);
    url.replace("%TIME%", tNow);
    pendingWebhookUrl = url;
  }
}

void processPendingWebhook() {
  if (pendingWebhookUrl.length() == 0) return;
  String url = pendingWebhookUrl;
  pendingWebhookUrl = "";

  WiFiClient client;
  HTTPClient http;
  http.setTimeout(1500); // Не вешаем станцию дольше 1.5 сек, если сервер недоступен
  if (http.begin(client, url)) {
    http.GET();
    http.end();
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
    case I2C_BMP280:
      if (bmp280.begin(addr)) {
        i2cValues[idx].val1 = bmp280.readTemperature();
        i2cValues[idx].val2 = bmp280.readPressure() / 133.322;
      }
      break;
    case I2C_AHT20:
      if (aht.begin(&Wire, 0, addr)) {
        sensors_event_t humidity, temp;
        aht.getEvent(&humidity, &temp);
        i2cValues[idx].val1 = temp.temperature;
        i2cValues[idx].val2 = humidity.relative_humidity;
      }
      break;
    case I2C_SHT41:
      if (sht4.begin(&Wire)) {
        sht4.setPrecision(SHT4X_HIGH_PRECISION);
        sensors_event_t humidity, temp;
        sht4.getEvent(&humidity, &temp);
        i2cValues[idx].val1 = temp.temperature;
        i2cValues[idx].val2 = humidity.relative_humidity;
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
      if (param == PARAM_HUM) {
        if (cfg.i2cDevices[i].type == I2C_AHT20 || cfg.i2cDevices[i].type == I2C_SHT41) {
          return i2cValues[i].val2;
        }
        return i2cValues[i].val3;
      }
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
