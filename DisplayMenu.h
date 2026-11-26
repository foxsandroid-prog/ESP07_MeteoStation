#pragma once
#include <LiquidCrystal_I2C_Menu.h>
#include "Config.h"
#include "Sensors.h"
#include "WebServer.h"

LiquidCrystal_I2C_Menu lcd(LCD_ADDR, LCD_COLS, LCD_ROWS);

unsigned long lastActivityTime = 0;
bool isBacklightOn = true;
int mainScrollOffset = 0;

uint8_t degreeChar[8] = {0x06, 0x09, 0x09, 0x06, 0x00, 0x00, 0x00, 0x00}; // Символ градуса
// Иконки 5x8 для LCD2004 (слоты CGRAM: 0 = питание, 1 = Wi-Fi)
uint8_t iconPlugChar[8]  = {0x0A, 0x0A, 0x1F, 0x1F, 0x0E, 0x04, 0x04, 0x00}; // Вилка / сеть 24В
uint8_t iconBattChar[8]  = {0x0E, 0x1F, 0x11, 0x1F, 0x1F, 0x1F, 0x1F, 0x00}; // Батарея (АКБ)
uint8_t iconApChar[8]    = {0x15, 0x15, 0x0E, 0x04, 0x04, 0x04, 0x04, 0x00}; // Антенна (режим AP)
uint8_t iconWifiOff[8]   = {0x00, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x00, 0x00}; // Крестик (Wi-Fi OFF / нет связи)
uint8_t iconSig1[8]      = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x10}; // 1 палочка (слабый сигнал)
uint8_t iconSig2[8]      = {0x00, 0x00, 0x00, 0x00, 0x08, 0x08, 0x18, 0x18}; // 2 палочки
uint8_t iconSig3[8]      = {0x00, 0x00, 0x04, 0x04, 0x0C, 0x0C, 0x1C, 0x1C}; // 3 палочки
uint8_t iconSig4[8]      = {0x02, 0x02, 0x06, 0x06, 0x0E, 0x0E, 0x1E, 0x1E}; // 4 палочки (отличный сигнал)

String formatUptimeShort() {
  unsigned long sec = millis() / 1000UL;
  unsigned long days = sec / 86400UL;
  unsigned long hrs  = (sec % 86400UL) / 3600UL;
  unsigned long mins = (sec % 3600UL) / 60UL;
  char buf[12];
  if (days > 0) snprintf(buf, sizeof(buf), "%lud%02luh", days, hrs);
  else snprintf(buf, sizeof(buf), "%02lu:%02lu", hrs, mins);
  return String(buf);
}


const char* NET_CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.";
const char* NAME_CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_ ";

// ================= ПОДСВЕТКА ЭКРАНА =================
void registerUserActivity() {
  lastActivityTime = millis();
  if (cfg.backlightMode == 2 && !isBacklightOn) {
    lcd.backlight();
    isBacklightOn = true;
  }
}

void updateBacklight() {
  if (cfg.backlightMode == 0) {
    if (!isBacklightOn) {
      lcd.backlight();
      isBacklightOn = true;
    }
  } else if (cfg.backlightMode == 1) {
    if (isBacklightOn) {
      lcd.noBacklight();
      isBacklightOn = false;
    }
  } else if (cfg.backlightMode == 2) {
    if (isBacklightOn && (millis() - lastActivityTime >= (unsigned long)cfg.backlightTimeout * 1000UL)) {
      lcd.noBacklight();
      isBacklightOn = false;
    }
  }
}

// ================= ФОРМАТИРОВАНИЕ И ГЛАВНЫЙ ЭКРАН =================
String formatValue(float val, const DisplayFormat &fmt, const char *unitStr, bool isTemp = true) {
  if (isnan(val)) return "ERR";
  String res = "";
  if (fmt.showPlusSign && val > 0.05) res += "+";
  if (fmt.showDecimals) res += String(val, 1);
  else res += String((int)round(val));
  if (fmt.showUnit) {
    if (isTemp) res += "\x07""C";
    else res += unitStr;
  }
  return res;
}

String makeCellText(uint8_t kind, uint8_t idx) {
  if (kind == 0) { // 1-Wire
    const DisplayFormat &f = cfg.owSensors[idx].fmt;
    String s = f.showName ? (String(f.name) + ": ") : "";
    return s + formatValue(owValues[idx], f, "\x07""C", true);
  }
  if (kind == 1) { // DHT
    const DhtSensorCfg &d = cfg.dhtSensors[idx];
    const DisplayFormat &f = d.fmt;
    String s = f.showName ? (String(f.name) + ": ") : "";
    bool first = true;
    if ((d.mode == 0 || d.mode == 1) && f.showP1) {
      s += formatValue(dhtTemp[idx], f, "\x07""C", true);
      first = false;
    }
    if ((d.mode == 0 || d.mode == 2) && f.showP2) {
      if (!first) s += " ";
      s += formatValue(dhtHum[idx], f, "%", false);
    }
    return s;
  }
  // I2C
  const I2cDeviceCfg &d = cfg.i2cDevices[idx];
  const DisplayFormat &f = d.fmt;
  String s = f.showName ? (String(f.name) + ": ") : "";
  if (!i2cValues[idx].online) return s + "OFFLINE";
  bool first = true;

  if (d.type == I2C_BMP180_085 || d.type == I2C_BME280 || d.type == I2C_BMP280) {
    if (f.showP1) { s += formatValue(i2cValues[idx].val1, f, "\x07""C", true); first = false; }
    if (f.showP2) { if (!first) s += " "; s += String((int)i2cValues[idx].val2) + (f.showUnit ? "mm" : ""); first = false; }
    if (d.type == I2C_BME280 && f.showP3) { if (!first) s += " "; s += String((int)i2cValues[idx].val3) + (f.showUnit ? "%" : ""); }
  } else if (d.type == I2C_AHT20 || d.type == I2C_SHT41) {
    if (f.showP1) { s += formatValue(i2cValues[idx].val1, f, "\x07""C", true); first = false; }
    if (f.showP2) { if (!first) s += " "; s += formatValue(i2cValues[idx].val2, f, "%", false); }
  } else if (d.type == I2C_BH1750 && f.showP1) {
    s += formatValue(i2cValues[idx].val1, f, "lx", false);
  } else if (d.type == I2C_PCF8574 && f.showP1) {
    uint8_t st = (uint8_t)i2cValues[idx].val1;
    for (int b = 7; b >= 0; b--) s += ((st >> b) & 1) ? "1" : "0";
  }
  return s;
}

void putInRow(String &row, uint8_t col, const String &text) {
  for (uint8_t k = 0; k < text.length() && (uint16_t)(col + k) < LCD_COLS; k++) {
    row[col + k] = text[k];
  }
}

void placeFixedText(String rowText[], uint8_t rowNext[], bool rowFixed[],
                    const String &text, int8_t prefRow, int8_t col) {
  int len = text.length();
  if (len == 0) return;

  if (col >= 0 && col < LCD_COLS) {
    putInRow(rowText[prefRow], (uint8_t)col, text);
    int endPos = min((int)LCD_COLS, col + len + 1);
    if (endPos > rowNext[prefRow]) rowNext[prefRow] = (uint8_t)endPos;
    rowFixed[prefRow] = true;
    return;
  }

  for (int r = prefRow; r < LCD_ROWS; r++) {
    if (rowNext[r] + len <= LCD_COLS) {
      putInRow(rowText[r], rowNext[r], text);
      rowNext[r] = min((int)LCD_COLS, (int)rowNext[r] + len + 1);
      rowFixed[r] = true;
      return;
    }
  }

  int r = prefRow, pos = 0;
  while (pos < len && r < LCD_ROWS) {
    int space = (int)LCD_COLS - (int)rowNext[r];
    if (space <= 0) {
      r++;
      continue;
    }
    int cut = pos + min(len - pos, space);
    if (cut < len) {
      int lastSpace = -1;
      for (int j = cut - 1; j > pos; j--) {
        if (text[j] == ' ') {
          lastSpace = j;
          break;
        }
      }
      if (lastSpace > pos) cut = lastSpace;
    }
    putInRow(rowText[r], rowNext[r], text.substring(pos, cut));
    rowNext[r] = min((int)LCD_COLS, (int)rowNext[r] + (cut - pos));
    rowFixed[r] = true;
    pos = cut;
    if (pos < len) {
      if (text[pos] == ' ') pos++;
      r++;
    }
  }
}

void drawMainScreen() {
  String rowText[LCD_ROWS];
  uint8_t rowNext[LCD_ROWS];
  bool rowFixed[LCD_ROWS];
  for (uint8_t r = 0; r < LCD_ROWS; r++) {
    rowText[r] = "                    ";
    rowNext[r] = 0;
    rowFixed[r] = false;
  }

  String autoLines[MAX_OW_SENSORS + MAX_DHT_SENSORS + MAX_I2C_DEVICES + 2];
  uint8_t totalAuto = 0;

  // Слот 1 (\x01): иконка питания (вилка или батарейка)
  lcd.createChar(1, externalPower24V ? iconPlugChar : iconBattChar);

  // Слот 2 (\x02): иконка Wi-Fi (антенна AP, уровень сигнала STA или крестик)
  if (cfg.net.wifiMode == 2) {
    lcd.createChar(2, iconApChar);
  } else if (cfg.net.wifiMode == 1 && WiFi.status() == WL_CONNECTED) {
    long rssi = WiFi.RSSI();
    if (rssi >= -60)      lcd.createChar(2, iconSig4);
    else if (rssi >= -70) lcd.createChar(2, iconSig3);
    else if (rssi >= -80) lcd.createChar(2, iconSig2);
    else                  lcd.createChar(2, iconSig1);
  } else {
    lcd.createChar(2, iconWifiOff);
  }

  // Формируем верхнюю строку ровно на 20 символов: "12:34 U:02:15 4.1V⚡📶"
  // 1. Всегда формируем и закрепляем строку 0 (верхняя системная строка)
  static bool screenAliveTick = false;
  screenAliveTick = !screenAliveTick;
  String timeShort = getFormattedTime().substring(0, 5);
  if (screenAliveTick) timeShort.replace(":", "%"); // Мигает "12:34" <-> "12%34" при каждой перерисовке
  char rfMark = hasUnreadRf ? '!' : ' ';           // "!" между временем и U:, если есть непросмотренный RF-сигнал
  String leftPart = timeShort + String(rfMark) + "U:" + formatUptimeShort();

  String rightPart = String(batteryVoltage, 1) + "V\x01\x02";
  int spaces = (int)LCD_COLS - (int)leftPart.length() - (int)rightPart.length();
  String sysLine = leftPart;
  for (int s = 0; s < max(1, spaces); s++) sysLine += " ";
  sysLine += rightPart;
  while (sysLine.length() < (unsigned)LCD_COLS) sysLine += " ";
  rowText[0] = sysLine;
  rowFixed[0] = true; // Жёстко фиксируем строку 0, чтобы прокрутка её не трогала

  // 2. Собираем датчики с фиксированными координатами (только в строки 1..3) и авто-список
  for (uint8_t pass = 0; pass < 2; pass++) {
    for (uint8_t k = 0; k < 3; k++) {
      int count = (k == 0) ? MAX_OW_SENSORS : (k == 1) ? MAX_DHT_SENSORS : MAX_I2C_DEVICES;
      for (int i = 0; i < count; i++) {
        const DisplayFormat *f = (k == 0) ? &cfg.owSensors[i].fmt :
                                 (k == 1) ? &cfg.dhtSensors[i].fmt : &cfg.i2cDevices[i].fmt;
        if (!f->enabled || !f->showOnMain) continue;

        if (pass == 0) {
          // Фиксированные датчики разрешаем только в строках 1..3, чтобы не затирать строку 0
          if (f->row < 1 || f->row >= LCD_ROWS) continue;
          placeFixedText(rowText, rowNext, rowFixed, makeCellText(k, i), f->row, f->col);
        } else {
          if (f->row != -1) continue;
          autoLines[totalAuto++] = makeCellText(k, i);
        }
      }
    }
  }

  // 3. Считаем сколько свободных строк осталось ниже 0-й (обычно 3 строки: 1, 2, 3)
  int freeRows = 0;
  for (uint8_t r = 1; r < LCD_ROWS; r++) {
    if (!rowFixed[r]) freeRows++;
  }

  // Ограничиваем прокрутку, чтобы не уезжать в пустоту
  int maxOffset = max(0, (int)totalAuto - freeRows);
  if (mainScrollOffset > maxOffset) mainScrollOffset = 0;
  if (mainScrollOffset < 0) mainScrollOffset = maxOffset;

  // 4. Заполняем авто-списком только строки 1..3
  int autoIdx = mainScrollOffset;
  for (uint8_t r = 1; r < LCD_ROWS; r++) {
    if (!rowFixed[r] && autoIdx < (int)totalAuto) {
      String line = autoLines[autoIdx++];
      while (line.length() < (unsigned)LCD_COLS) line += " ";
      rowText[r] = line;
    }
  }

  // 5. Выводим все 4 строки на дисплей
  for (uint8_t r = 0; r < LCD_ROWS; r++) {
    lcd.setCursor(0, r);
    lcd.print(rowText[r].substring(0, LCD_COLS));
  }
}

// ================= ВСПОМОГАТЕЛЬНЫЕ ДИАЛОГИ =================
bool editBoolSetting(const String &title, bool currentVal, const char* offLabel = "OFF / NO", const char* onLabel = "ON / YES") {
  const char* opts[3] = {offLabel, onLabel, "< Back"};
  uint8_t res = lcd.selectVal(title, opts, 3, false, currentVal ? 1 : 0);
  if (res == 0) return false;
  if (res == 1) return true;
  return currentVal;
}

void editIpAddress(const String &title, uint8_t ipArr[4]) {
  uint8_t pos = 0;
  while (true) {
    String items[5];
    for (int i = 0; i < 4; i++) {
      items[i] = "Octet " + String(i + 1) + ": " + String(ipArr[i]);
    }
    items[4] = "< Back";
    pos = lcd.selectVal(title, items, 5, false, pos);
    if (pos == 4) break;
    ipArr[pos] = (uint8_t)lcd.inputVal("Octet " + String(pos + 1) + ":", 0, 255, (int)ipArr[pos], 1);
  }
}

void trimTrailingSpaces(char *str, size_t maxLen) {
  str[maxLen - 1] = '\0';
  int len = strlen(str);
  while (len > 0 && str[len - 1] == ' ') {
    str[--len] = '\0';
  }
}

// ================= МЕНЮ СЕТЕВЫХ НАСТРОЕК (WI-FI / IP / WEB) =================
void menuNetwork() {
  uint8_t pos = 0;
  bool netChanged = false;

  while (true) {
    const char* modeNames[3] = {"OFF", "Client (STA)", "Access Point"};
    String statusStr = "OFF";
    if (cfg.net.wifiMode == 2) statusStr = "AP Active";
    else if (cfg.net.wifiMode == 1) statusStr = (WiFi.status() == WL_CONNECTED) ? "Connected" : "Connecting...";

    String ipStr = (cfg.net.wifiMode == 2) ? WiFi.softAPIP().toString() :
                   (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "0.0.0.0");

    String items[11] = {
      "Mode: " + String(modeNames[cfg.net.wifiMode]),
      "SSID>" + String(cfg.net.ssid),
      "Pass>" + String(cfg.net.password),
      "Stat>" + statusStr,
      "IP>" + ipStr,
      "IP Mode: " + String(cfg.net.useStaticIp ? "Static" : "DHCP"),
      "Static IP >",
      "Gateway & Mask >",
      "TZ: UTC" + (cfg.net.timezone >= 0 ? "+" + String(cfg.net.timezone) : String(cfg.net.timezone)),
      "WPas>" + String(cfg.net.webPass),
      "< Back"
    };

    pos = lcd.selectVal("Network & Wi-Fi", items, 11, false, pos);
    if (pos == 10) break;

    if (pos == 0) {
      const char* modes[4] = {"OFF", "Client (STA)", "Access Point", "< Back"};
      uint8_t m = lcd.selectVal("Wi-Fi Mode", modes, 4, false, cfg.net.wifiMode);
      if (m < 3 && m != cfg.net.wifiMode) {
        cfg.net.wifiMode = m;
        netChanged = true;
      }
    } else if (pos == 1) {
      lcd.inputStrVal("Wi-Fi SSID:", cfg.net.ssid, 16, NET_CHARS);
      trimTrailingSpaces(cfg.net.ssid, sizeof(cfg.net.ssid));
      netChanged = true;
    } else if (pos == 2) {
      lcd.inputStrVal("Wi-Fi Password:", cfg.net.password, 16, NET_CHARS);
      trimTrailingSpaces(cfg.net.password, sizeof(cfg.net.password));
      netChanged = true;
    } else if (pos == 3) {
      const char* ok[1] = {"< Back"};
      lcd.selectVal(statusStr, ok, 1, false, 0);
    } else if (pos == 4) {
      const char* ok[1] = {"< Back"};
      lcd.selectVal(ipStr, ok, 1, false, 0);
    } else if (pos == 5) {
      bool st = editBoolSetting("IP Configuration", cfg.net.useStaticIp, "DHCP (Auto)", "Static IP");
      if (st != cfg.net.useStaticIp) {
        cfg.net.useStaticIp = st;
        netChanged = true;
      }
    } else if (pos == 6) {
      editIpAddress("Static IP", cfg.net.ip);
      netChanged = true;
    } else if (pos == 7) {
      uint8_t gmPos = 0;
      while (true) {
        const char* gmItems[4] = {"Gateway IP >", "Subnet Mask >", "DNS Server >", "< Back"};
        gmPos = lcd.selectVal("Gateway & Mask", gmItems, 4, false, gmPos);
        if (gmPos == 3) break;
        if (gmPos == 0) editIpAddress("Gateway", cfg.net.gateway);
        else if (gmPos == 1) editIpAddress("Subnet Mask", cfg.net.subnet);
        else if (gmPos == 2) editIpAddress("DNS Server", cfg.net.dns);
        netChanged = true;
      }
    } else if (pos == 8) {
      cfg.net.timezone = (int8_t)lcd.inputVal("Timezone (-12..14):", -12, 14, (int)cfg.net.timezone, 1);
      configTime((long)cfg.net.timezone * 3600L, 0, "pool.ntp.org", "time.nist.gov");
    } else if (pos == 9) {
      lcd.inputStrVal("Web Password:", cfg.net.webPass, 16, NET_CHARS);
      trimTrailingSpaces(cfg.net.webPass, sizeof(cfg.net.webPass));
    }
    saveConfig();
  }
  if (netChanged) {
    lcd.clear();
    lcd.setCursor(2, 1);
    lcd.print("Applying Wi-Fi...");
    applyNetworkSettings();
  }
}

// ================= НАСТРОЙКА ФОРМАТА ДАТЧИКА =================
void editDisplayFormat(DisplayFormat &fmt, uint8_t pCount,
                       const char *p1Name, const char *p2Name, const char *p3Name) {
  uint8_t pos = 0;
  while (true) {
    String items[13];
    uint8_t n = 0;
    items[n++] = String("Enabled: ") + (fmt.enabled ? "YES" : "NO");
    items[n++] = String("Name: ") + String(fmt.name);
    items[n++] = String("On Main: ") + (fmt.showOnMain ? "YES" : "NO");
    items[n++] = String("Show Name: ") + (fmt.showName ? "YES" : "NO");
    items[n++] = String("Row: ") + (fmt.row == -1 ? String("Auto") : String(fmt.row));
    items[n++] = String("Col: ") + (fmt.col == -1 ? String("Auto") : String(fmt.col));
    items[n++] = String(p1Name) + ": " + (fmt.showP1 ? "ON" : "OFF");
    if (pCount >= 2) items[n++] = String(p2Name) + ": " + (fmt.showP2 ? "ON" : "OFF");
    if (pCount >= 3) items[n++] = String(p3Name) + ": " + (fmt.showP3 ? "ON" : "OFF");
    uint8_t decIdx = n;
    items[n++] = String("Decimals: ") + (fmt.showDecimals ? "ON" : "OFF");
    items[n++] = String("Plus Sign: ") + (fmt.showPlusSign ? "ON" : "OFF");
    items[n++] = String("Units: ") + (fmt.showUnit ? "ON" : "OFF");
    items[n++] = "< Back";
    pos = lcd.selectVal("Sensor Format", items, n, false, pos);
    if (pos == n - 1) break;
    if (pos == 0) fmt.enabled = editBoolSetting("Enabled", fmt.enabled);
    else if (pos == 1) lcd.inputStrVal("Name:", fmt.name, 8, NAME_CHARS);
    else if (pos == 2) fmt.showOnMain = editBoolSetting("Show on Main", fmt.showOnMain);
    else if (pos == 3) fmt.showName = editBoolSetting("Show Name", fmt.showName);
    else if (pos == 4) fmt.row = (int8_t)lcd.inputVal("Row (-1..3):", -1, 3, (int)fmt.row, 1);
    else if (pos == 5) fmt.col = (int8_t)lcd.inputVal("Col (-1..19):", -1, 19, (int)fmt.col, 1);
    else if (pos == 6) fmt.showP1 = editBoolSetting(String(p1Name), fmt.showP1);
    else if (pos == 7 && pCount >= 2) fmt.showP2 = editBoolSetting(String(p2Name), fmt.showP2);
    else if (pos == 8 && pCount >= 3) fmt.showP3 = editBoolSetting(String(p3Name), fmt.showP3);
    else if (pos == decIdx) fmt.showDecimals = editBoolSetting("Decimals", fmt.showDecimals);
    else if (pos == decIdx + 1) fmt.showPlusSign = editBoolSetting("Plus Sign", fmt.showPlusSign);
    else if (pos == decIdx + 2) fmt.showUnit = editBoolSetting("Show Units", fmt.showUnit);
    saveConfig();
  }
}

// ================= ПОБИТОВОЕ УПРАВЛЕНИЕ PCF8574 =================
void menuPcfPins(uint8_t devIdx) {
  uint8_t chPos = 0;
  while (true) {
    bool prevEn = cfg.i2cDevices[devIdx].fmt.enabled;
    cfg.i2cDevices[devIdx].fmt.enabled = true;
    readSingleI2c(devIdx);
    cfg.i2cDevices[devIdx].fmt.enabled = prevEn;
    uint8_t curState = (uint8_t)i2cValues[devIdx].val1;

    constexpr uint8_t PIN_ITEMS_CNT = 9;
    String pinItems[PIN_ITEMS_CNT];
    for (int b = 0; b < 8; b++) {
      bool isOut = (cfg.i2cDevices[devIdx].pcfDirMask >> b) & 1;
      String nm = String(cfg.i2cDevices[devIdx].pcfPinNames[b]);
      if (isOut) {
        bool isOn = (cfg.i2cDevices[devIdx].pcfOutMask >> b) & 1;
        bool isInv = (cfg.i2cDevices[devIdx].pcfInvMask >> b) & 1;
        pinItems[b] = "P" + String(b) + "[" + nm + "]:" + (isOn ? "ON" : "OFF") + (isInv ? "(I)" : "");
      } else {
        bool inState = (curState >> b) & 1;
        pinItems[b] = "P" + String(b) + "[" + nm + "]:IN=" + (inState ? "1" : "0");
      }
    }
    pinItems[8] = "< Back";

    chPos = lcd.selectVal("PCF8574 Channels", pinItems, PIN_ITEMS_CNT, false, chPos);
    if (chPos == 8) break;

    uint8_t b = chPos;
    bool isOut = (cfg.i2cDevices[devIdx].pcfDirMask >> b) & 1;
    uint8_t actPos = 0;

    while (true) {
      constexpr uint8_t MAX_ACT = 5;
      String pinActs[MAX_ACT];
      uint8_t n = 0;
      int stateIdx = -1, invIdx = -1;

      if (isOut) {
        bool isOn = (cfg.i2cDevices[devIdx].pcfOutMask >> b) & 1;
        stateIdx = n;
        pinActs[n++] = String("State: ") + (isOn ? "ON (1)" : "OFF (0)");
      } else {
        bool inState = (curState >> b) & 1;
        pinActs[n++] = String("Input: ") + (inState ? "HIGH" : "LOW");
      }

      int dirIdx = n;
      pinActs[n++] = String("Dir: ") + (isOut ? "OUT" : "IN");

      if (isOut) {
        bool isInv = (cfg.i2cDevices[devIdx].pcfInvMask >> b) & 1;
        invIdx = n;
        pinActs[n++] = String("Invert: ") + (isInv ? "YES (LOW)" : "NO (HIGH)");
      }

      int renIdx = n;
      pinActs[n++] = "Rename Channel";
      pinActs[n++] = "< Back";

      actPos = lcd.selectVal("P" + String(b) + " " + String(cfg.i2cDevices[devIdx].pcfPinNames[b]), pinActs, n, false, actPos);
      if (actPos == n - 1) break;

      if (actPos == stateIdx) {
        bool newState = editBoolSetting("P" + String(b) + " Relay", (cfg.i2cDevices[devIdx].pcfOutMask >> b) & 1, "OFF (0)", "ON (1)");
        if (newState) cfg.i2cDevices[devIdx].pcfOutMask |= (1 << b);
        else cfg.i2cDevices[devIdx].pcfOutMask &= ~(1 << b);
        applyPcfOutputs(devIdx);
        saveConfig();
      } else if (actPos == dirIdx) {
        if (isOut) cfg.i2cDevices[devIdx].pcfDirMask &= ~(1 << b);
        else cfg.i2cDevices[devIdx].pcfDirMask |= (1 << b);
        applyPcfOutputs(devIdx);
        saveConfig();
        break;
      } else if (actPos == invIdx) {
        bool newInv = editBoolSetting("Invert P" + String(b), (cfg.i2cDevices[devIdx].pcfInvMask >> b) & 1, "NO (Direct)", "YES (Active LOW)");
        if (newInv) cfg.i2cDevices[devIdx].pcfInvMask |= (1 << b);
        else cfg.i2cDevices[devIdx].pcfInvMask &= ~(1 << b);
        applyPcfOutputs(devIdx);
        saveConfig();
      } else if (actPos == renIdx) {
        lcd.inputStrVal("Pin Name:", cfg.i2cDevices[devIdx].pcfPinNames[b], 6, NAME_CHARS);
        saveConfig();
      }
    }
  }
}

// ================= КОНСТРУКТОР ПРАВИЛ АВТОМАТИЗАЦИИ =================
void editSingleRule(uint8_t rIdx) {
  AutoRule &r = cfg.rules[rIdx];
  const char* paramLabels[6] = {"Temp", "Hum", "Press", "Lux", "BAT", "RF"};
  const char* opLabels[5] = {">", "<", ">=", "<=", "=="};
  uint8_t pos = 0;
  while (true) {
    String items[10] = {
      "Enabled: " + String(r.enabled ? "YES" : "NO"),
      "Sensor: " + String(r.sensorName),
      "Param: " + String(paramLabels[r.param]),
      "Cond: " + String(opLabels[r.op]),
      "Value: " + String(r.threshold, 1),
      "PCF Dev: Slot " + String(r.pcfIdx + 1),
      "Relay Pin: P" + String(r.pcfBit),
      "Action: " + String(r.targetState ? "ON (1)" : "OFF (0)"),
      "Else Invert: " + String(r.invertElse ? "YES" : "NO"),
      "< Back"
    };
    pos = lcd.selectVal("Rule #" + String(rIdx + 1), items, 10, false, pos);
    if (pos == 9) break;
    if (pos == 0) {
      r.enabled = editBoolSetting("Rule Enabled", r.enabled);
    } else if (pos == 1) {
      lcd.inputStrVal("Sensor Name:", r.sensorName, 8, NAME_CHARS);
    } else if (pos == 2) {
      const char* pOpts[7] = {"Temp", "Hum", "Press", "Lux", "BAT", "RF", "< Back"};
      uint8_t p = lcd.selectVal("Parameter", pOpts, 7, false, (uint8_t)r.param);
      if (p < 6) r.param = (RuleParam)p;
    } else if (pos == 3) {
      const char* oOpts[6] = {"> (More)", "< (Less)", ">= (More/Eq)", "<= (Less/Eq)", "== (Equal)", "< Back"};
      uint8_t o = lcd.selectVal("Condition", oOpts, 6, false, (uint8_t)r.op);
      if (o < 5) r.op = (RuleOp)o;
    } else if (pos == 4) {
      int val10 = lcd.inputVal("Threshold x10:", -500, 10000, (int)round(r.threshold * 10.0), 5);
      r.threshold = val10 / 10.0;
    } else if (pos == 5) {
      r.pcfIdx = (uint8_t)lcd.inputVal("I2C Slot (1..6):", 1, MAX_I2C_DEVICES, (int)r.pcfIdx + 1, 1) - 1;
    } else if (pos == 6) {
      r.pcfBit = (uint8_t)lcd.inputVal("PCF Pin (0..7):", 0, 7, (int)r.pcfBit, 1);
    } else if (pos == 7) {
      r.targetState = editBoolSetting("If True Action", r.targetState, "Turn OFF (0)", "Turn ON (1)");
    } else if (pos == 8) {
      r.invertElse = editBoolSetting("Else Invert?", r.invertElse);
    }
    saveConfig();
  }
}

void menuRules() {
  uint8_t pos = 0;
  const char* opSym[5] = {">", "<", ">=", "<=", "=="};
  while (true) {
    String items[MAX_RULES + 1];
    for (int i = 0; i < MAX_RULES; i++) {
      if (cfg.rules[i].enabled) {
        items[i] = String(i + 1) + "." + String(cfg.rules[i].sensorName) +
                   String(opSym[cfg.rules[i].op]) + String(cfg.rules[i].threshold, 0) +
                   "->P" + String(cfg.rules[i].pcfBit);
      } else {
        items[i] = String(i + 1) + ". [Disabled]";
      }
    }
    items[MAX_RULES] = "< Back";
    pos = lcd.selectVal("Automation Rules", items, MAX_RULES + 1, false, pos);
    if (pos == MAX_RULES) break;
    editSingleRule(pos);
  }
}

// ================= МЕНЮ 1-WIRE, I2C И СИСТЕМЫ =================

String formatOneWireId(const uint8_t addr[8]) {
  char buf[17];
  for (uint8_t i = 0; i < 8; i++) {
    snprintf(buf + i * 2, 3, "%02X", addr[i]);
  }
  return String(buf);
}

void menuOneWire() {
  uint8_t pos = 0;
  const char* items[3] = {"1. Scan 1-Wire Bus", "2. Saved Sensors", "< Back"};
  while (true) {
    pos = lcd.selectVal("1-Wire (GPIO14)", items, 3, false, pos);
    if (pos == 2) break;
    if (pos == 0) {
      lcd.clear(); lcd.setCursor(0, 1); lcd.print("Scanning 1-Wire...");
      owSensorsBus.begin();
      delay(20);
      uint8_t addr[8];
      uint8_t found = 0;
      String foundList[MAX_OW_SENSORS + 1];

      for (uint8_t attempt = 0; attempt < 3 && found == 0; attempt++) {
        oneWire.reset_search();
        delay(10);
        while (oneWire.search(addr)) {
          yield();
          if (OneWire::crc8(addr, 7) != addr[7]) continue;
          int slot = -1;
          for (int i = 0; i < MAX_OW_SENSORS; i++) {
            if (memcmp(cfg.owSensors[i].addr, addr, 8) == 0) {
              slot = i;
              break;
            }
          }
          if (slot == -1) {
            for (int i = 0; i < MAX_OW_SENSORS; i++) {
              if (cfg.owSensors[i].addr[0] == 0) {
                slot = i;
                memcpy(cfg.owSensors[i].addr, addr, 8);
                cfg.owSensors[i].fmt.enabled = true;
                cfg.owSensors[i].fmt.showOnMain = true;
                break;
              }
            }
          }
          if (slot != -1) {
            owSensorsBus.requestTemperaturesByAddress(addr);
            delay(750);
            owValues[slot] = owSensorsBus.getTempC(addr);
            String tStr = isnan(owValues[slot]) ? "ERR" : (String(owValues[slot], 1) + "C");
            String idFull = formatOneWireId(addr);
            foundList[found] = idFull.substring(0, 6) + ".." + idFull.substring(12, 16) + " " + tStr;
            found++;
          }
        }
      }
      saveConfig();
      foundList[found] = "< Back";
      uint8_t fPos = 0;
      while (true) {
        fPos = lcd.selectVal("Found 1W: " + String(found), foundList, found + 1, false, fPos);
        if (fPos == found) break;
        int realIdx = -1, cnt = 0;
        for (int i = 0; i < MAX_OW_SENSORS; i++) {
          if (cfg.owSensors[i].addr[0] != 0) {
            if (cnt == fPos) { realIdx = i; break; }
            cnt++;
          }
        }
        if (realIdx != -1) {
          const char* ok[1] = {"< Back"};
          lcd.selectVal(formatOneWireId(cfg.owSensors[realIdx].addr), ok, 1, false, 0);
        }
      }
    } else if (pos == 1) {
      uint8_t sPos = 0;
      while (true) {
        String sList[MAX_OW_SENSORS + 1];
        for (int i = 0; i < MAX_OW_SENSORS; i++) {
          if (cfg.owSensors[i].addr[0] != 0) {
            String id = formatOneWireId(cfg.owSensors[i].addr);
            sList[i] = String(cfg.owSensors[i].fmt.name) + " [" + id.substring(12, 16) + "]";
          } else {
            sList[i] = "Slot " + String(i + 1) + " [Empty]";
          }
        }
        sList[MAX_OW_SENSORS] = "< Back";
        sPos = lcd.selectVal("Saved DS18B20", sList, MAX_OW_SENSORS + 1, false, sPos);
        if (sPos == MAX_OW_SENSORS) break;
        if (cfg.owSensors[sPos].addr[0] != 0) {
          const char* sub[2] = {"Edit Format >", "< Back"};
          uint8_t act = lcd.selectVal(formatOneWireId(cfg.owSensors[sPos].addr), sub, 2, false, 0);
          if (act == 0) {
            editDisplayFormat(cfg.owSensors[sPos].fmt, 1, "Temp", "", "");
          }
        }
      }
    }
  }
}

void menuI2C() {
  uint8_t pos = 0;
  const char* items[3] = {"1. Scan I2C Bus", "2. Device Settings", "< Back"};
  const char* tNames[8] = {"None", "BMP180", "BME280", "BMP280", "AHT20", "SHT41", "BH1750", "PCF8574"};

  while (true) {
    pos = lcd.selectVal("I2C Devices", items, 3, false, pos);
    if (pos == 2) break;
    if (pos == 0) {
      lcd.clear(); lcd.setCursor(0, 1); lcd.print("Scanning I2C...");
      uint8_t found = 0;
      String foundList[MAX_I2C_DEVICES + 2];

      for (uint8_t addr = 1; addr < 127; addr++) {
        yield();
        if (isI2cAddressIgnored(addr)) continue;

        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
          I2cDevType det = I2C_NONE;
          if (addr == 0x23 || addr == 0x5C) det = I2C_BH1750;
          else if (addr == 0x38) det = I2C_AHT20;
          else if (addr == 0x44 || addr == 0x45) det = I2C_SHT41;
          else if (addr == 0x76 || addr == 0x77) det = I2C_BMP280;
          else if ((addr >= 0x20 && addr <= 0x27) || (addr >= 0x39 && addr <= 0x3F)) det = I2C_PCF8574;

          int slot = -1;
          for (int i = 0; i < MAX_I2C_DEVICES; i++) if (cfg.i2cDevices[i].addr == addr) slot = i;
          if (slot == -1) {
            for (int i = 0; i < MAX_I2C_DEVICES; i++) {
              if (cfg.i2cDevices[i].addr == 0) {
                slot = i;
                cfg.i2cDevices[i].addr = addr;
                cfg.i2cDevices[i].type = det;
                cfg.i2cDevices[i].fmt.enabled = true;
                cfg.i2cDevices[i].fmt.showOnMain = true;
                break;
              }
            }
          }
          char hexBuf[6];
          snprintf(hexBuf, sizeof(hexBuf), "0x%02X", addr);
          uint8_t curType = (slot != -1) ? cfg.i2cDevices[slot].type : det;
          foundList[found++] = String(hexBuf) + " (" + String(tNames[curType]) + ")";
        }
      }
      saveConfig();
      initPeripherals();

      foundList[found] = "< Back";
      uint8_t fPos = 0;
      while (true) {
        fPos = lcd.selectVal("Found I2C: " + String(found), foundList, found + 1, false, fPos);
        if (fPos == found) break;
      }
    } else if (pos == 1) {
      uint8_t dPos = 0;
      while (true) {
        String dList[MAX_I2C_DEVICES + 1];
        for (int i = 0; i < MAX_I2C_DEVICES; i++) {
          if (cfg.i2cDevices[i].addr != 0) {
            char hexBuf[6];
            snprintf(hexBuf, sizeof(hexBuf), "0x%02X", cfg.i2cDevices[i].addr);
            dList[i] = String(cfg.i2cDevices[i].fmt.name) + " [" + String(hexBuf) + "]";
          } else {
            dList[i] = "Slot " + String(i + 1) + " [Empty]";
          }
        }
        dList[MAX_I2C_DEVICES] = "< Back";
        dPos = lcd.selectVal("Select I2C", dList, MAX_I2C_DEVICES + 1, false, dPos);
        if (dPos == MAX_I2C_DEVICES) break;
        if (cfg.i2cDevices[dPos].addr != 0) {
          uint8_t cardPos = 0;
          while (true) {
            char hexBuf[6];
            snprintf(hexBuf, sizeof(hexBuf), "0x%02X", cfg.i2cDevices[dPos].addr);
            String cItems[6];
            uint8_t cn = 0;
            cItems[cn++] = "Addr: " + String(hexBuf);
            cItems[cn++] = "Type: " + String(tNames[cfg.i2cDevices[dPos].type]);
            int pcfIdx = -1;
            if (cfg.i2cDevices[dPos].type == I2C_PCF8574) {
              pcfIdx = cn;
              cItems[cn++] = "PCF8574 Channels >";
            }
            int fmtIdx = cn;
            cItems[cn++] = "Display Format >";
            int delIdx = cn;
            cItems[cn++] = "Delete Device";
            cItems[cn++] = "< Back";

            cardPos = lcd.selectVal(cfg.i2cDevices[dPos].fmt.name, cItems, cn, false, cardPos);
            if (cardPos == cn - 1) break;
            if (cardPos == 1) {
              const char* types[9] = {"None", "BMP180", "BME280", "BMP280", "AHT20", "SHT41", "BH1750", "PCF8574", "< Back"};
              uint8_t t = lcd.selectVal("Device Type", types, 9, false, (uint8_t)cfg.i2cDevices[dPos].type);
              if (t < 8) {
                cfg.i2cDevices[dPos].type = (I2cDevType)t;
                saveConfig();
                initPeripherals();
              }
            } else if (cardPos == pcfIdx) {
              menuPcfPins(dPos);
            } else if (cardPos == fmtIdx) {
              editDisplayFormat(cfg.i2cDevices[dPos].fmt, 3, "P1", "P2", "P3");
            } else if (cardPos == delIdx) {
              if (editBoolSetting("Delete " + String(hexBuf) + "?", false)) {
                cfg.i2cDevices[dPos].addr = 0;
                cfg.i2cDevices[dPos].type = I2C_NONE;
                cfg.i2cDevices[dPos].fmt.enabled = false;
                saveConfig();
                break;
              }
            }
          }
        }
      }
    }
  }
}

void menuSystem() {
  uint8_t pos = 0;
  while (true) {
    const char* blNames[] = {"Always ON", "Always OFF", "Auto Timer"};
    String rtcBadge = rtcOnline ? ("RTC:OK " + getFormattedTime()) : "RTC:OFFLINE (0x68)";
    String items[9] = {
      "Backlight: " + String(blNames[cfg.backlightMode]),
      "Auto-Off: " + String(cfg.backlightTimeout) + "s",
      "Poll Period: " + String(cfg.pollIntervalSec) + "s",
      "Servo Angle: " + String(cfg.servoAngle) + "d",
      rtcBadge,
      "Sync NTP -> RTC",
      "Last RF: " + String(lastRfCode),
      "Reset Defaults",
      "< Back"
    };
    pos = lcd.selectVal("System & LCD", items, 9, false, pos);
    if (pos == 8) break;
    if (pos == 0) {
      const char* modes[4] = {"Always ON", "Always OFF", "Auto Timer", "< Back"};
      uint8_t m = lcd.selectVal("Backlight Mode", modes, 4, false, cfg.backlightMode);
      if (m < 3) {
        cfg.backlightMode = m;
        updateBacklight();
      }
    } else if (pos == 1) {
      cfg.backlightTimeout = (uint16_t)lcd.inputVal("Timeout (s):", 5, 3600, (int)cfg.backlightTimeout, 5);
    } else if (pos == 2) {
      cfg.pollIntervalSec = (uint16_t)lcd.inputVal("Poll (s):", 1, 3600, (int)cfg.pollIntervalSec, 1);
    } else if (pos == 3) {
      uint8_t ang = (uint8_t)lcd.inputVal("Servo (0..180):", 0, 180, (int)cfg.servoAngle, 5);
      setServoAngle(ang);
    } else if (pos == 4) {
      initRtc();
      String rInfo = rtcOnline ? ("0x68 OK " + getFormattedTime()) : "0x68 Not Found!";
      const char* ok[1] = {"< Back"};
      lcd.selectVal(rInfo, ok, 1, false, 0);
    } else if (pos == 5) {
      syncRtcWithNtp();
      const char* ok[1] = {"< Back"};
      lcd.selectVal(ntpSynced ? "RTC Synced!" : "NTP Not Ready", ok, 1, false, 0);
    } else if (pos == 7) {
      if (editBoolSetting("Reset All?", false)) {
        setDefaultConfig();
        saveConfig();
      }
    }
    saveConfig();
  }
}

void menuRfLog() {
  hasUnreadRf = false; // Сбрасываем индикатор "!" на главном экране
  uint8_t pos = 0;
  while (true) {
    String items[MAX_RF_LOG + 1];
    for (int i = 0; i < MAX_RF_LOG; i++) {
      if (rfLog[i].code == 0) {
        items[i] = String(i + 1) + ". --- empty ---";
      } else {
        // Формат: "1.16:45:10 1234567" (ровно помещается в строку 20 символов)
        items[i] = String(i + 1) + "." + String(rfLog[i].timeStr) + " " + String(rfLog[i].code);
      }
    }
    items[MAX_RF_LOG] = "< Back";
    pos = lcd.selectVal("RF 433 Log (Last 5)", items, MAX_RF_LOG + 1, false, pos);
    if (pos == MAX_RF_LOG) break;

    // При нажатии на конкретный сигнал показываем полную информацию (протокол и битность)
    if (rfLog[pos].code != 0) {
      String info = "P" + String(rfLog[pos].proto) + " " + String(rfLog[pos].bitLen) + "b #" + String(rfLog[pos].code);
      const char* ok[1] = {"< Back"};
      lcd.selectVal(info, ok, 1, false, 0);
    }
  }
}

void openMainMenu() {
  pollPhase = POLL_IDLE;
  uint8_t pos = 0;
  const char* mainMenuItems[8] = {
    "1. 1-Wire (DS18B20)",
    "2. DHT22 Sensor",
    "3. I2C Devices",
    "4. Network & Wi-Fi",
    "5. Automation Rules",
    "6. System & LCD",
    "7. RF 433 Log",
    "< Exit to Monitor"
  };
  while (true) {
    registerUserActivity();
    server.handleClient();
    pos = lcd.selectVal("MAIN MENU", mainMenuItems, 8, false, pos);
    if (pos == 7) break;
    switch (pos) {
      case 0: menuOneWire(); break;
      case 1: editDisplayFormat(cfg.dhtSensors[0].fmt, 2, "Temp", "Hum", ""); break;
      case 2: menuI2C(); break;
      case 3: menuNetwork(); break;
      case 4: menuRules(); break;
      case 5: menuSystem(); break;
      case 6: menuRfLog(); break;
    }
  }
  lcd.clear();
  drawMainScreen();
  lastPollCycleStart = millis();
  startOneWireConversion();
}
