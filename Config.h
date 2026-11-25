#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <RTClib.h>
#include <time.h>

// ================= КОНФИГУРАЦИЯ ОБОРУДОВАНИЯ =================
#define LCD_ADDR 0x27
#define RTC_ADDR 0x68
#define LCD_COLS 20
#define LCD_ROWS 4

// Пины ESP-07
#define pinSDA   4
#define pinSCL   5
#define pinCLK   12  // CLK энкодера на GPIO12 (подтяжка 10 кОм к GND) (был на GPIO2 с неподходящей поддяжкой)
#define pinDT    13  // DT энкодера на GPIO15 (подтяжка 10 кОм к GND),
// меняю на 13 пин и подтяжку к 3.3В и общий к gnd
#define pinSW    0   // Кнопка энкодера (подтяжка 10к к +3.3В, замыкает на GND)
#define pinOW    14  // 1-Wire (DS18B20)
#define pinDHT   2   // Single-Wire (DHT22), из-за энкодера перенесён на GPIO2 (подтяжка 4.7 кОм к +3.3 В) (был на 12)
#define pinRF    15  // Приемник SRX887, перенес на 15 с 13, что бы энкодер подключить
#define pinServo 16  // Сервопривод
#define pinADC   A0  // Контроль АКБ 18650

#define MAX_OW_SENSORS  16
#define MAX_DHT_SENSORS 2
#define MAX_I2C_DEVICES 8
#define MAX_RULES 8
#define EEPROM_MAGIC    0x0F

// ================= СТРУКТУРЫ НАСТРОЕК =================
struct DisplayFormat {
  bool enabled;
  char name[9];
  bool showOnMain;
  bool showName;
  int8_t row;
  int8_t col;
  bool showP1;
  bool showP2;
  bool showP3;
  bool showDecimals;
  bool showPlusSign;
  bool showUnit;
};

struct OneWireSensorCfg {
  uint8_t addr[8];
  DisplayFormat fmt;
};

struct DhtSensorCfg {
  uint8_t pin;
  uint8_t type;
  uint8_t mode;
  DisplayFormat fmt;
};

enum I2cDevType : uint8_t {
  I2C_NONE = 0,
  I2C_BMP180_085,
  I2C_BME280,
  I2C_BMP280,
  I2C_AHT20,
  I2C_SHT41,
  I2C_BH1750,
  I2C_PCF8574
};

struct I2cDeviceCfg {
  uint8_t addr;
  I2cDevType type;
  uint8_t pcfOutMask;
  uint8_t pcfDirMask;
  uint8_t pcfInvMask; // Маска инверсии выходов (для реле HL-52S с управлением LOW)
  char pcfPinNames[8][7];
  DisplayFormat fmt;
};

struct NetworkConfig {
  uint8_t wifiMode;       // 0 = OFF, 1 = STA (клиент), 2 = AP (своя точка)
  char ssid[17];          // До 16 символов + '\0'
  char password[17];      // До 16 символов + '\0'
  bool useStaticIp;       // false = DHCP, true = Статика
  uint8_t ip[4];
  uint8_t gateway[4];
  uint8_t subnet[4];
  uint8_t dns[4];
  char webUser[17];       // Логин Web-управления
  char webPass[17];       // Пароль Web-управления
  int8_t timezone;        // Часовой пояс (-12..+14)
};

enum RuleParam : uint8_t {
  PARAM_TEMP = 0, // Температура (1-Wire, DHT, BMP, BME)
  PARAM_HUM,      // Влажность (DHT, BME280)
  PARAM_PRESS,    // Давление мм рт. ст. (BMP, BME280)
  PARAM_LUX,      // Освещенность (BH1750)
  PARAM_BAT,      // Напряжение АКБ 18650 (ввести имя "BAT")
  PARAM_RF        // Последний код 433 МГц (ввести имя "RF")
};

enum RuleOp : uint8_t {
  OP_GT = 0, // >
  OP_LT,     // <
  OP_GTE,    // >=
  OP_LTE,    // <=
  OP_EQ      // ==
};

struct AutoRule {
  bool enabled;         // Правило активно
  char sensorName[9];   // Имя датчика (например, "DS_1", "BME_1", "BAT", "RF")
  RuleParam param;      // Параметр (temp, hum, press, lux, bat, rf)
  RuleOp op;            // Условие (>, <, >=, <=, ==)
  float threshold;      // Пороговое значение
  uint8_t pcfIdx;       // Индекс расширителя PCF8574 (0..MAX_I2C_DEVICES-1)
  uint8_t pcfBit;       // Номер канала PCF8574 (0..7)
  bool targetState;     // true = ВКЛ (1), false = ВЫКЛ (0) при выполнении условия
  bool invertElse;      // true = инвертировать выход, если условие НЕ выполняется
};

#define MAX_RF_LOG 5
#define MAX_RF_RULES 4

struct RfLogEntry {
  unsigned long code;
  uint16_t bitLen;
  uint8_t proto;
  char timeStr[10]; // "HH:MM:SS"
};

RfLogEntry rfLog[MAX_RF_LOG];

struct RfActionRule {
  bool enabled;
  unsigned long code;      // Код сигнала (например, 1234567)
  char label[12];          // Имя датчика (например, "Дверь")
  uint8_t pcfIdx;          // Индекс PCF8574 (255 = не управлять реле)
  uint8_t pcfBit;          // Пин 0..7
  uint8_t relayAction;     // 0 = нет, 1 = ВКЛ, 2 = ВЫКЛ, 3 = ПЕРЕКЛЮЧИТЬ
  bool sendWebhook;        // Отправлять ли HTTP GET на внешний URL
};

struct SystemConfig {
  uint8_t magic;
  uint8_t backlightMode;     // 0 = ВКЛ, 1 = ВЫКЛ, 2 = Таймер
  uint16_t backlightTimeout;
  uint16_t pollIntervalSec;
  uint8_t servoAngle;        // Текущий угол сервопривода (0..180)
  uint8_t powerSensePcfIdx;  // Индекс PCF8574 для контроля 24В (255 = выкл)
  uint8_t powerSensePcfBit;  // Номер пина PCF8574 (0..7)
  NetworkConfig net;
  OneWireSensorCfg owSensors[MAX_OW_SENSORS];
  DhtSensorCfg dhtSensors[MAX_DHT_SENSORS];
  I2cDeviceCfg i2cDevices[MAX_I2C_DEVICES];
  AutoRule rules[MAX_RULES];
  uint8_t i2cIgnoreAddrs[8]; // Исключаемые I2C-адреса при сканировании (0 = пустой слот)
  char rfWebhookUrl[96];               // Шаблон ссылки, например: http://192.168.1%
  RfActionRule rfRules[MAX_RF_RULES];  // Реакции на конкретные коды
} cfg;

// ================= РАБОЧИЕ ДАННЫЕ =================
float owValues[MAX_OW_SENSORS];
float dhtTemp[MAX_DHT_SENSORS];
float dhtHum[MAX_DHT_SENSORS];

struct I2cData {
  float val1;
  float val2;
  float val3;
  bool online;
} i2cValues[MAX_I2C_DEVICES];

float batteryVoltage = 0.0;
bool externalPower24V = true;
unsigned long lastRfCode = 0;
unsigned long lastRfTime = 0;

RTC_DS1307 rtc;
bool rtcOnline = false;
bool ntpSynced = false;

// ================= EEPROM И НАСТРОЙКИ ПО УМОЛЧАНИЮ =================
void setDefaultConfig() {
  memset(&cfg, 0, sizeof(SystemConfig));
  cfg.magic = EEPROM_MAGIC;
  cfg.backlightMode = 0;
  cfg.backlightTimeout = 30;
  cfg.pollIntervalSec = 5;
  cfg.servoAngle = 90;
  cfg.powerSensePcfIdx = 0;
  cfg.powerSensePcfBit = 7;

  cfg.rfWebhookUrl[0] = '\0';
  memset(cfg.rfRules, 0, sizeof(cfg.rfRules));

  // Сеть по умолчанию: Точка доступа
  cfg.net.wifiMode = 2; // AP
  strlcpy(cfg.net.ssid, "Meteo_ESP07", sizeof(cfg.net.ssid));
  strlcpy(cfg.net.password, "12345678", sizeof(cfg.net.password));
  cfg.net.useStaticIp = false;
  cfg.net.ip[0] = 192; cfg.net.ip[1] = 168; cfg.net.ip[2] = 1; cfg.net.ip[3] = 50;
  cfg.net.gateway[0] = 192; cfg.net.gateway[1] = 168; cfg.net.gateway[2] = 1; cfg.net.gateway[3] = 1;
  cfg.net.subnet[0] = 255; cfg.net.subnet[1] = 255; cfg.net.subnet[2] = 255; cfg.net.subnet[3] = 0;
  cfg.net.dns[0] = 8; cfg.net.dns[1] = 8; cfg.net.dns[2] = 8; cfg.net.dns[3] = 8;
  strlcpy(cfg.net.webUser, "admin", sizeof(cfg.net.webUser));
  strlcpy(cfg.net.webPass, "admin", sizeof(cfg.net.webPass));
  cfg.net.timezone = 3;

  for (int i = 0; i < MAX_OW_SENSORS; i++) {
    snprintf(cfg.owSensors[i].fmt.name, 9, "DS_%d", i + 1);
    cfg.owSensors[i].fmt.row = -1;
    cfg.owSensors[i].fmt.col = -1;
    cfg.owSensors[i].fmt.showName = true;
    cfg.owSensors[i].fmt.showDecimals = true;
    cfg.owSensors[i].fmt.showUnit = true;
    cfg.owSensors[i].fmt.showP1 = true;
    owValues[i] = NAN;
  }

  for (int i = 0; i < MAX_DHT_SENSORS; i++) {
    snprintf(cfg.dhtSensors[i].fmt.name, 9, "DHT_%d", i + 1);
    cfg.dhtSensors[i].pin = (i == 0) ? pinDHT : 0;
    cfg.dhtSensors[i].type = 22;
    cfg.dhtSensors[i].fmt.row = -1;
    cfg.dhtSensors[i].fmt.col = -1;
    cfg.dhtSensors[i].fmt.showName = true;
    cfg.dhtSensors[i].fmt.showDecimals = true;
    cfg.dhtSensors[i].fmt.showUnit = true;
    cfg.dhtSensors[i].fmt.showP1 = true;
    cfg.dhtSensors[i].fmt.showP2 = true;
    dhtTemp[i] = NAN;
    dhtHum[i] = NAN;
  }

// список адресов игнора в i2c
   memset(cfg.i2cIgnoreAddrs, 0, sizeof(cfg.i2cIgnoreAddrs));
   cfg.i2cIgnoreAddrs[0] = 0x50;  // 0x50 и 0x57 стандартные адреса EEPROM AT24C32 на модулях часов RTC

  for (int i = 0; i < MAX_I2C_DEVICES; i++) {
    snprintf(cfg.i2cDevices[i].fmt.name, 9, "I2C_%d", i + 1);
    cfg.i2cDevices[i].fmt.row = -1;
    cfg.i2cDevices[i].fmt.col = -1;
    cfg.i2cDevices[i].fmt.showName = true;
    cfg.i2cDevices[i].fmt.showDecimals = true;
    cfg.i2cDevices[i].fmt.showUnit = true;
    cfg.i2cDevices[i].fmt.showP1 = true;
    cfg.i2cDevices[i].fmt.showP2 = true;
    cfg.i2cDevices[i].fmt.showP3 = true;
    cfg.i2cDevices[i].pcfDirMask = 0x7F; // P0..P6 = выходы (0..6), P7 = вход (7)
    cfg.i2cDevices[i].pcfInvMask = 0x00; // По умолчанию без инверсии (можно включить в меню/Web)

    for (int p = 0; p < 8; p++) {
      snprintf(cfg.i2cDevices[i].pcfPinNames[p], 7, "PIN_%d", p);
    }
  }
  for (int i = 0; i < MAX_RULES; i++) {
    cfg.rules[i].enabled = false;
    strlcpy(cfg.rules[i].sensorName, "DS_1", sizeof(cfg.rules[i].sensorName));
    cfg.rules[i].param = PARAM_TEMP;
    cfg.rules[i].op = OP_GT;
    cfg.rules[i].threshold = 25.0;
    cfg.rules[i].pcfIdx = 0;
    cfg.rules[i].pcfBit = i % 8;
    cfg.rules[i].targetState = true;
    cfg.rules[i].invertElse = true;
  }
}

bool saveConfig() {
  cfg.magic = EEPROM_MAGIC;
  EEPROM.put(0, cfg);
  bool ok = EEPROM.commit();
  delay(50);
  return ok;
}

void loadConfig() {
//  EEPROM.begin(sizeof(SystemConfig) + 64);
  EEPROM.begin(2048);
  EEPROM.get(0, cfg);
  if (cfg.magic != EEPROM_MAGIC) {
    setDefaultConfig();
    saveConfig();
  }
  if (cfg.pollIntervalSec == 0 || cfg.pollIntervalSec > 3600) cfg.pollIntervalSec = 5;
  for (int i = 0; i < MAX_OW_SENSORS; i++) owValues[i] = NAN;
  for (int i = 0; i < MAX_DHT_SENSORS; i++) {
    dhtTemp[i] = NAN;
    dhtHum[i] = NAN;
  }
}

bool isI2cAddressIgnored(uint8_t addr) {
  if (addr == LCD_ADDR || addr == RTC_ADDR) return true;
  for (uint8_t i = 0; i < 8; i++) {
    if (cfg.i2cIgnoreAddrs[i] != 0 && cfg.i2cIgnoreAddrs[i] == addr) return true;
  }
  return false;
}

// ================= УПРАВЛЕНИЕ PCF8574 =================
uint8_t pcfPortByte(uint8_t idx) {
  uint8_t outByte = 0;
  for (uint8_t b = 0; b < 8; b++) {
    if ((cfg.i2cDevices[idx].pcfDirMask >> b) & 1) {
      bool logicalOn = (cfg.i2cDevices[idx].pcfOutMask >> b) & 1;
      bool inverted  = (cfg.i2cDevices[idx].pcfInvMask >> b) & 1;
      bool physHigh  = inverted ? !logicalOn : logicalOn;
      if (physHigh) outByte |= (1 << b);
    } else {
      outByte |= (1 << b); // Для режима входа PCF8574 требует записать 1 (подтяжка)
    }
  }
  return outByte;
}

void applyPcfOutputs(uint8_t idx) {
  uint8_t addr = cfg.i2cDevices[idx].addr;
  if (addr == 0 || addr == LCD_ADDR || addr == RTC_ADDR) return;
  Wire.beginTransmission(addr);
  Wire.write(pcfPortByte(idx));
  Wire.endTransmission();
}

// ================= WI-FI И СИНХРОНИЗАЦИЯ RTC/NTP =================
void applyNetworkSettings() {
  WiFi.disconnect(true);
  delay(100);

  if (cfg.net.wifiMode == 0) {
    WiFi.mode(WIFI_OFF);
    return;
  }

  if (cfg.net.wifiMode == 2) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(cfg.net.ssid, cfg.net.password);
    return;
  }

  // Режим клиента (STA)
  WiFi.mode(WIFI_STA);
  if (cfg.net.useStaticIp) {
    IPAddress ip(cfg.net.ip[0], cfg.net.ip[1], cfg.net.ip[2], cfg.net.ip[3]);
    IPAddress gw(cfg.net.gateway[0], cfg.net.gateway[1], cfg.net.gateway[2], cfg.net.gateway[3]);
    IPAddress sn(cfg.net.subnet[0], cfg.net.subnet[1], cfg.net.subnet[2], cfg.net.subnet[3]);
    IPAddress dns(cfg.net.dns[0], cfg.net.dns[1], cfg.net.dns[2], cfg.net.dns[3]);
    WiFi.config(ip, gw, sn, dns);
  }
  WiFi.begin(cfg.net.ssid, cfg.net.password);
  configTime((long)cfg.net.timezone * 3600L, 0, "pool.ntp.org", "time.nist.gov");
}

void initRtc() {
  Wire.beginTransmission(RTC_ADDR);
  if (Wire.endTransmission() == 0 && rtc.begin()) {
    rtcOnline = true;
    if (!rtc.isrunning()) {
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }
  } else {
    rtcOnline = false;
  }
}

void syncRtcWithNtp() {
  if (cfg.net.wifiMode != 1 || WiFi.status() != WL_CONNECTED) return;
  time_t now = time(nullptr);
  struct tm *t = localtime(&now);
  if (t && (t->tm_year + 1900) >= 2025) {
    Wire.beginTransmission(RTC_ADDR);
    if (Wire.endTransmission() == 0) {
      rtcOnline = true;
      rtc.adjust(DateTime(t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
                          t->tm_hour, t->tm_min, t->tm_sec));
      ntpSynced = true;
    }
  }
}

String getFormattedTime() {
  Wire.beginTransmission(RTC_ADDR);
  if (Wire.endTransmission() == 0) {
    rtcOnline = true;
    DateTime now = rtc.now();
    char buf[10];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
    return String(buf);
  }
  rtcOnline = false;
  time_t now = time(nullptr);
  struct tm *t = localtime(&now);
  if (t && (t->tm_year + 1900) >= 2025) {
    char buf[10];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", t->tm_hour, t->tm_min, t->tm_sec);
    return String(buf);
  }
  return "--:--:--";
}
