#pragma once
#include <ESP8266WebServer.h>
#include "Config.h"
#include "Sensors.h"

void updateBacklight();
void recoverI2cBusAndDisplay();

ESP8266WebServer server(80);

bool checkWebAuth() {
  if (!server.authenticate(cfg.net.webUser, cfg.net.webPass)) {
    server.requestAuthentication();
    return false;
  }
  return true;
}

void handleApiSensor() {
  if (!server.hasArg("name")) {
    server.send(400, "text/plain", "Missing name");
    return;
  }
  String target = server.arg("name");
  String param = server.hasArg("param") ? server.arg("param") : "";

  if (target.equalsIgnoreCase("BAT")) {
    server.send(200, "text/plain", String(batteryVoltage, 2));
    return;
  }
  if (target.equalsIgnoreCase("PWR24")) {
    server.send(200, "text/plain", externalPower24V ? "1" : "0");
    return;
  }
  if (target.equalsIgnoreCase("RF")) {
    server.send(200, "text/plain", String(lastRfCode));
    return;
  }

  for (uint8_t i = 0; i < MAX_OW_SENSORS; i++) {
    if (cfg.owSensors[i].fmt.enabled &&
        target.equalsIgnoreCase(cfg.owSensors[i].fmt.name)) {
      if (isnan(owValues[i])) server.send(200, "text/plain", "NaN");
      else server.send(200, "text/plain", String(owValues[i], 2));
      return;
    }
  }

  for (uint8_t i = 0; i < MAX_DHT_SENSORS; i++) {
    if (cfg.dhtSensors[i].fmt.enabled &&
        target.equalsIgnoreCase(cfg.dhtSensors[i].fmt.name)) {
      if (param.equalsIgnoreCase("hum")) {
        server.send(200, "text/plain",
                    isnan(dhtHum[i]) ? "NaN" : String(dhtHum[i], 1));
      } else {
        server.send(200, "text/plain",
                    isnan(dhtTemp[i]) ? "NaN" : String(dhtTemp[i], 1));
      }
      return;
    }
  }

  for (uint8_t i = 0; i < MAX_I2C_DEVICES; i++) {
    if (cfg.i2cDevices[i].fmt.enabled &&
        target.equalsIgnoreCase(cfg.i2cDevices[i].fmt.name)) {
      if (!i2cValues[i].online) {
        server.send(200, "text/plain", "OFFLINE");
        return;
      }
      if (cfg.i2cDevices[i].type == I2C_PCF8574) {
        server.send(200, "text/plain", String((uint8_t)i2cValues[i].val1));
      } else if (param.equalsIgnoreCase("press")) {
        server.send(200, "text/plain", String(i2cValues[i].val2, 1));
      } else if (param.equalsIgnoreCase("hum")) {
        server.send(200, "text/plain", String(i2cValues[i].val3, 1));
      } else {
        server.send(200, "text/plain", String(i2cValues[i].val1, 1));
      }
      return;
    }
  }
  server.send(404, "text/plain", "Sensor not found");
}

void handleApiRelay() {
  if (!checkWebAuth()) return;
  if (!server.hasArg("dev") || !server.hasArg("pin")) {
    server.send(400, "text/plain", "Bad args");
    return;
  }
  uint8_t dev = server.arg("dev").toInt();
  uint8_t pin = server.arg("pin").toInt();
  if (dev >= MAX_I2C_DEVICES || pin > 7 ||
      cfg.i2cDevices[dev].type != I2C_PCF8574) {
    server.send(404, "text/plain", "Invalid PCF");
    return;
  }
  bool cur = (cfg.i2cDevices[dev].pcfOutMask >> pin) & 1;
  bool next = server.hasArg("state") ? (server.arg("state").toInt() == 1) : !cur;
  if (next) cfg.i2cDevices[dev].pcfOutMask |= (1 << pin);
  else cfg.i2cDevices[dev].pcfOutMask &= ~(1 << pin);
  applyPcfOutputs(dev);
  saveConfig();
  server.send(200, "text/plain", next ? "1" : "0");
}

void handleApiAll() {
  String json = "{\"time\":\"" + getFormattedTime() + "\",";
  String wStr = "OFF";
  if (cfg.net.wifiMode == 2) wStr = "AP (" + WiFi.softAPIP().toString() + ")";
  else if (cfg.net.wifiMode == 1) {
    wStr = (WiFi.status() == WL_CONNECTED) ?
           ("STA (" + WiFi.localIP().toString() + ")") : "STA (Поиск...)";
  }
  json += "\"wifi\":\"" + wStr + "\",";

  long rssi = (cfg.net.wifiMode == 1 && WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;
  unsigned long sec = millis() / 1000UL;
  char upBuf[24];
  snprintf(upBuf, sizeof(upBuf), "%luд %02lu:%02lu:%02lu",
           sec / 86400UL, (sec % 86400UL) / 3600UL, (sec % 3600UL) / 60UL, sec % 60UL);

  json += "\"rssi\":" + String(rssi) + ",";
  json += "\"wmode\":" + String(cfg.net.wifiMode) + ",";
  json += "\"uptime\":\"" + String(upBuf) + "\",";
  json += "\"bat\":" + String(batteryVoltage, 2) + ",";
  json += "\"pwr24\":" + String(externalPower24V ? 1 : 0) + ",";

  json += "\"rf\":" + String(lastRfCode) + ",";
  json += "\"servo\":" + String(cfg.servoAngle) + ",\"sensors\":[";

  bool first = true;
  for (uint8_t i = 0; i < MAX_OW_SENSORS; i++) {
    if (!cfg.owSensors[i].fmt.enabled) continue;
    if (!first) json += ",";
    first = false;
    json += "{\"name\":\"" + String(cfg.owSensors[i].fmt.name) + "\",\"val\":\"";
    json += isnan(owValues[i]) ? "ERR" : (String(owValues[i], 1) + " C");
    json += "\"}";
  }
  for (uint8_t i = 0; i < MAX_DHT_SENSORS; i++) {
    if (!cfg.dhtSensors[i].fmt.enabled) continue;
    if (!first) json += ",";
    first = false;
    json += "{\"name\":\"" + String(cfg.dhtSensors[i].fmt.name) + "\",\"val\":\"";
    json += (isnan(dhtTemp[i]) ? "ERR" : String(dhtTemp[i], 1)) + " C / ";
    json += (isnan(dhtHum[i]) ? "ERR" : String(dhtHum[i], 1)) + " %";
    json += "\"}";
  }
  for (uint8_t i = 0; i < MAX_I2C_DEVICES; i++) {
    if (!cfg.i2cDevices[i].fmt.enabled) continue;
    if (!first) json += ",";
    first = false;
    json += "{\"name\":\"" + String(cfg.i2cDevices[i].fmt.name) + "\",\"val\":\"";
    if (!i2cValues[i].online) json += "OFFLINE";
    else if (cfg.i2cDevices[i].type == I2C_BME280) {
      json += String(i2cValues[i].val1, 1) + "C " +
              String((int)i2cValues[i].val2) + "mm " +
              String((int)i2cValues[i].val3) + "%";
    } else if (cfg.i2cDevices[i].type == I2C_BMP180_085) {
      json += String(i2cValues[i].val1, 1) + "C " +
              String((int)i2cValues[i].val2) + "mm";
    } else if (cfg.i2cDevices[i].type == I2C_BH1750) {
      json += String(i2cValues[i].val1, 0) + " lx";
    } else if (cfg.i2cDevices[i].type == I2C_PCF8574) {
      json += "BIN:" + String((uint8_t)i2cValues[i].val1, BIN);
    }
    json += "\"}";
  }
  json += "],\"relays\":[";
  bool rFirst = true;
  for (uint8_t d = 0; d < MAX_I2C_DEVICES; d++) {
    if (!cfg.i2cDevices[d].fmt.enabled ||
        cfg.i2cDevices[d].type != I2C_PCF8574) continue;
    for (uint8_t b = 0; b < 8; b++) {
      if ((cfg.i2cDevices[d].pcfDirMask >> b) & 1) {
        if (!rFirst) json += ",";
        rFirst = false;
        bool st = (cfg.i2cDevices[d].pcfOutMask >> b) & 1;
        json += "{\"dev\":" + String(d) + ",\"pin\":" + String(b) +
                ",\"name\":\"" + String(cfg.i2cDevices[d].pcfPinNames[b]) +
                "\",\"st\":" + String(st ? 1 : 0) + "}";
      }
    }
  }
  json += "]}";
  server.send(200, "application/json", json);
}

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>ESP-07 Meteo</title><style>
body{font-family:sans-serif;max-width:600px;margin:auto;padding:15px;background:#121212;color:#eee}
table{width:100%;border-collapse:collapse;margin-top:10px}
td,th{border:1px solid #333;padding:8px;text-align:left}
a.btn{display:inline-block;padding:8px 14px;background:#1976d2;color:#fff;text-decoration:none;border-radius:4px;margin:12px 6px 0 0}
button.rl{margin:4px;padding:10px 14px;color:#fff;border:none;border-radius:4px;cursor:pointer}
</style></head><body>
<h2>Метеостанция ESP-07</h2>
<div id='sys'>Загрузка...</div>
<table><thead><tr><th>Датчик</th><th>Значение</th><th>API</th></tr></thead><tbody id='tb'></tbody></table>
<div id='rel'></div>
<a class='btn' href='/sensors'>Датчики</a>
<a class='btn' href='/rules'>Автоматизация</a>
<a class='btn' href='/net'>Сеть и Wi-Fi</a>
<a class='btn' href='/sys'>Система и RTC</a>
<script>
function tog(d,p){fetch('/api/relay?dev='+d+'&pin='+p).then(()=>upd());}
function upd(){fetch('/api/all').then(r=>r.json()).then(d=>{
let wIcon = d.wmode==2 ? '📡 AP' : (d.rssi!=0 ? ('📶 '+(d.rssi>=-60?'4/4':d.rssi>=-70?'3/4':d.rssi>=-80?'2/4':'1/4')+' ('+d.rssi+' dBm)') : '❌ Нет связи');
let pIcon = d.pwr24 ? '🔌 Сеть (24V)' : '🔋 АКБ ('+d.bat+'V)';
document.getElementById('sys').innerHTML='Время: <b>'+d.time+'</b> | Uptime: <b>'+d.uptime+'</b><br>Wi-Fi: <b>'+d.wifi+'</b> ['+wIcon+']<br>Питание: <b>'+pIcon+'</b> | АКБ: <b>'+d.bat+'V</b> | RF: <b>'+d.rf+'</b>';
let h='';d.sensors.forEach(s=>{h+='<tr><td>'+s.name+'</td><td><b>'+s.val+'</b></td><td><a style="color:#64b5f6" href="/api/sensor?name='+s.name+'">GET</a></td></tr>';});
document.getElementById('tb').innerHTML=h;
let rh='';if(d.relays.length){rh+='<h3>Реле (PCF8574)</h3>';d.relays.forEach(r=>{
let bg=r.st?'#2e7d32':'#424242';
rh+='<button class="rl" style="background:'+bg+'" onclick="tog('+r.dev+','+r.pin+')">'+r.name+' (P'+r.pin+'): '+(r.st?'ВКЛ':'ВЫКЛ')+'</button>';
});}document.getElementById('rel').innerHTML=rh;
});}
upd(); setInterval(upd,3000);
</script></body></html>
)rawliteral";

void handleRoot() { server.send_P(200, "text/html", INDEX_HTML); }

void handleRules() {
  if (!checkWebAuth()) return;
  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'><title>Rules</title>";
  html += "<style>body{font-family:sans-serif;max-width:650px;margin:auto;padding:12px;background:#121212;color:#eee}";
  html += ".card{border:1px solid #333;padding:10px;margin-bottom:10px;border-radius:6px}input,select{padding:4px;margin:2px}</style>";
  html += "</head><body><h2>Правила автоматизации</h2><form method='POST' action='/save_rules'>";

  const char* pN[] = {"Temp", "Hum", "Press", "Lux", "BAT", "RF"};
  const char* oN[] = {"&gt;", "&lt;", "&gt;=", "&lt;=", "=="};

  for (int i = 0; i < MAX_RULES; i++) {
    AutoRule &r = cfg.rules[i];
    html += "<div class='card'><b>#" + String(i + 1) + "</b> ";
    html += "<input type='checkbox' name='en" + String(i) + "'" + String(r.enabled ? " checked" : "") + "> Вкл | ";
    html += "Датчик: <input name='sn" + String(i) + "' size='8' maxlength='8' value='" + String(r.sensorName) + "'> ";
    html += "<select name='pr" + String(i) + "'>";
    for (int p = 0; p < 6; p++)
      html += "<option value='" + String(p) + "'" + String(r.param == p ? " selected" : "") + ">" + String(pN[p]) + "</option>";
    html += "</select> <select name='op" + String(i) + "'>";
    for (int o = 0; o < 5; o++)
      html += "<option value='" + String(o) + "'" + String(r.op == o ? " selected" : "") + ">" + String(oN[o]) + "</option>";
    html += "</select> <input name='th" + String(i) + "' size='6' value='" + String(r.threshold, 1) + "'><br>";
    html += "PCF слот (1-6): <input type='number' min='1' max='6' name='dv" + String(i) + "' value='" + String(r.pcfIdx + 1) + "'> ";
    html += "Пин (0-7): <input type='number' min='0' max='7' name='pn" + String(i) + "' value='" + String(r.pcfBit) + "'> ";
    html += "<select name='st" + String(i) + "'><option value='1'" + String(r.targetState ? " selected" : "") + ">ВКЛ (1)</option>";
    html += "<option value='0'" + String(!r.targetState ? " selected" : "") + ">ВЫКЛ (0)</option></select> ";
    html += "<input type='checkbox' name='iv" + String(i) + "'" + String(r.invertElse ? " checked" : "") + "> Инверт. иначе</div>";
  }
  html += "<button style='padding:10px 18px;background:#2e7d32;color:#fff;border:none' type='submit'>Сохранить правила</button>";
  html += "</form><br><a style='color:#64b5f6' href='/'>На главную</a></body></html>";
  server.send(200, "text/html", html);
}

void handleSaveRules() {
  if (!checkWebAuth()) return;
  for (int i = 0; i < MAX_RULES; i++) {
    AutoRule &r = cfg.rules[i];
    r.enabled = server.hasArg("en" + String(i));
    if (server.hasArg("sn" + String(i))) strlcpy(r.sensorName, server.arg("sn" + String(i)).c_str(), sizeof(r.sensorName));
    if (server.hasArg("pr" + String(i))) r.param = (RuleParam)server.arg("pr" + String(i)).toInt();
    if (server.hasArg("op" + String(i))) r.op = (RuleOp)server.arg("op" + String(i)).toInt();
    if (server.hasArg("th" + String(i))) r.threshold = server.arg("th" + String(i)).toFloat();
    if (server.hasArg("dv" + String(i))) r.pcfIdx = constrain(server.arg("dv" + String(i)).toInt() - 1, 0, MAX_I2C_DEVICES - 1);
    if (server.hasArg("pn" + String(i))) r.pcfBit = constrain(server.arg("pn" + String(i)).toInt(), 0, 7);
    if (server.hasArg("st" + String(i))) r.targetState = (server.arg("st" + String(i)).toInt() == 1);
    r.invertElse = server.hasArg("iv" + String(i));
  }
  saveConfig();
  evaluateAutomationRules();
  server.send(200, "text/html", "<meta charset='utf-8'>Правила сохранены! <a href='/rules'>Назад</a> | <a href='/'>На главную</a>");
}

void handleSettings() {
  if (!checkWebAuth()) return;
  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'><title>Settings</title>";
  html += "<style>body{font-family:sans-serif;max-width:500px;margin:auto;padding:15px;background:#121212;color:#eee}";
  html += "input,select{width:100%;padding:6px;margin:4px 0 10px;box-sizing:border-box}";
  html += "button{padding:10px 16px;background:#2e7d32;color:#fff;border:none;border-radius:4px;cursor:pointer}</style>";
  html += "</head><body><h2>Настройки системы</h2><form method='POST' action='/save'>";

  html += "Режим Wi-Fi:<select name='wmode'>";
  html += "<option value='0'" + String(cfg.net.wifiMode == 0 ? " selected" : "") + ">OFF</option>";
  html += "<option value='1'" + String(cfg.net.wifiMode == 1 ? " selected" : "") + ">Клиент (STA)</option>";
  html += "<option value='2'" + String(cfg.net.wifiMode == 2 ? " selected" : "") + ">Точка доступа (AP)</option></select>";

  html += "SSID (до 16 симв):<input name='ssid' maxlength='16' value='" + String(cfg.net.ssid) + "'>";
  html += "Пароль Wi-Fi (до 16):<input name='wpass' type='password' maxlength='16' value='" + String(cfg.net.password) + "'>";
  html += "IP режим:<select name='dhcp'><option value='0'" + String(!cfg.net.useStaticIp ? " selected" : "") + ">DHCP</option>";
  html += "<option value='1'" + String(cfg.net.useStaticIp ? " selected" : "") + ">Статический IP</option></select>";

  char ipBuf[20];
  snprintf(ipBuf, sizeof(ipBuf), "%d.%d.%d.%d", cfg.net.ip[0], cfg.net.ip[1], cfg.net.ip[2], cfg.net.ip[3]);
  html += "Стат. IP:<input name='ip' value='" + String(ipBuf) + "'>";
  snprintf(ipBuf, sizeof(ipBuf), "%d.%d.%d.%d", cfg.net.gateway[0], cfg.net.gateway[1], cfg.net.gateway[2], cfg.net.gateway[3]);
  html += "Шлюз (Gateway):<input name='gw' value='" + String(ipBuf) + "'>";
  snprintf(ipBuf, sizeof(ipBuf), "%d.%d.%d.%d", cfg.net.subnet[0], cfg.net.subnet[1], cfg.net.subnet[2], cfg.net.subnet[3]);
  html += "Маска (Subnet):<input name='sn' value='" + String(ipBuf) + "'>";

  html += "Часовой пояс (UTC):<input type='number' name='tz' value='" + String(cfg.net.timezone) + "'>";
  html += "Период опроса (сек):<input type='number' name='poll' value='" + String(cfg.pollIntervalSec) + "'>";
  html += "Угол сервопривода (0..180):<input type='number' name='servo' min='0' max='180' value='" + String(cfg.servoAngle) + "'>";
  html += "Web Логин:<input name='wuser' maxlength='16' value='" + String(cfg.net.webUser) + "'>";
  html += "Web Пароль:<input name='webpass' maxlength='16' value='" + String(cfg.net.webPass) + "'>";

  html += "24V контроль PCF слот (0=выкл, 1..6):<input type='number' name='psdev' min='0' max='6' value='" + String(cfg.powerSensePcfIdx == 255 ? 0 : cfg.powerSensePcfIdx + 1) + "'>";
  html += "24V контроль PCF пин (0..7):<input type='number' name='psbit' min='0' max='7' value='" + String(cfg.powerSensePcfBit) + "'>";

  for (uint8_t d = 0; d < MAX_I2C_DEVICES; d++) {
    if (cfg.i2cDevices[d].fmt.enabled && cfg.i2cDevices[d].type == I2C_PCF8574) {
      html += "<div style='border:1px solid #333;padding:8px;margin-bottom:10px'><b>Инверсия " + String(cfg.i2cDevices[d].fmt.name) + ":</b><br>";
      for (uint8_t b = 0; b < 8; b++) {
        if ((cfg.i2cDevices[d].pcfDirMask >> b) & 1) {
          bool inv = (cfg.i2cDevices[d].pcfInvMask >> b) & 1;
          String nm = "inv_" + String(d) + "_" + String(b);
          html += "<label style='margin-right:10px'><input style='width:auto' type='checkbox' name='" + nm + "'" + String(inv ? " checked" : "") + "> P" + String(b) + "</label>";
        }
      }
      html += "</div>";
    }
  }

  html += "<button type='submit'>Сохранить и применить</button></form><br>";
  html += "<a style='color:#64b5f6' href='/'>На главную</a></body></html>";

  server.send(200, "text/html", html);
}

void parseIpString(const String &s, uint8_t dst[]) {
  IPAddress addr;
  if (addr.fromString(s)) for (int i = 0; i < 4; i++) dst[i] = addr[i];
}

void handleSave() {
  if (!checkWebAuth()) return;
  bool netChanged = false;
  if (server.hasArg("wmode")) {
    uint8_t m = server.arg("wmode").toInt();
    if (m != cfg.net.wifiMode) { cfg.net.wifiMode = m; netChanged = true; }
  }
  if (server.hasArg("ssid") && server.arg("ssid") != String(cfg.net.ssid)) {
    strlcpy(cfg.net.ssid, server.arg("ssid").c_str(), sizeof(cfg.net.ssid));
    netChanged = true;
  }
  if (server.hasArg("wpass") && server.arg("wpass") != String(cfg.net.password)) {
    strlcpy(cfg.net.password, server.arg("wpass").c_str(), sizeof(cfg.net.password));
    netChanged = true;
  }
  if (server.hasArg("dhcp")) {
    bool st = (server.arg("dhcp").toInt() == 1);
    if (st != cfg.net.useStaticIp) { cfg.net.useStaticIp = st; netChanged = true; }
  }
  if (server.hasArg("ip")) parseIpString(server.arg("ip"), cfg.net.ip);
  if (server.hasArg("gw")) parseIpString(server.arg("gw"), cfg.net.gateway);
  if (server.hasArg("sn")) parseIpString(server.arg("sn"), cfg.net.subnet);
  if (server.hasArg("tz")) cfg.net.timezone = server.arg("tz").toInt();
  if (server.hasArg("poll")) cfg.pollIntervalSec = constrain(server.arg("poll").toInt(), 1, 3600);
  if (server.hasArg("servo")) setServoAngle(server.arg("servo").toInt());
  if (server.hasArg("wuser")) strlcpy(cfg.net.webUser, server.arg("wuser").c_str(), sizeof(cfg.net.webUser));
  if (server.hasArg("webpass")) strlcpy(cfg.net.webPass, server.arg("webpass").c_str(), sizeof(cfg.net.webPass));

  if (server.hasArg("psdev")) {
    int v = server.arg("psdev").toInt();
    cfg.powerSensePcfIdx = (v <= 0) ? 255 : constrain(v - 1, 0, MAX_I2C_DEVICES - 1);
  }
  if (server.hasArg("psbit")) cfg.powerSensePcfBit = constrain(server.arg("psbit").toInt(), 0, 7);
  for (uint8_t d = 0; d < MAX_I2C_DEVICES; d++) {
    if (cfg.i2cDevices[d].fmt.enabled && cfg.i2cDevices[d].type == I2C_PCF8574) {
      for (uint8_t b = 0; b < 8; b++) {
        if ((cfg.i2cDevices[d].pcfDirMask >> b) & 1) {
          String nm = "inv_" + String(d) + "_" + String(b);
          if (server.hasArg(nm)) cfg.i2cDevices[d].pcfInvMask |= (1 << b);
          else cfg.i2cDevices[d].pcfInvMask &= ~(1 << b);
        }
      }
    applyPcfOutputs(d);
    }
  }
  saveConfig();
  server.send(200, "text/html", "<meta charset='utf-8'>Сохранено! <a href='/'>На главную</a>");
  if (netChanged) { delay(300); applyNetworkSettings(); }
}

void handleNetSettings() {
  if (!checkWebAuth()) return;
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");

  server.sendContent(F("<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>Сеть</title>"
    "<style>body{font-family:sans-serif;max-width:520px;margin:auto;padding:15px;background:#121212;color:#eee}"
    "input,select{width:100%;padding:6px;margin:4px 0 10px;box-sizing:border-box}"
    "button{padding:10px 16px;background:#2e7d32;color:#fff;border:none;border-radius:4px;cursor:pointer}"
    "a{color:#64b5f6}</style></head><body><h2>Сетевые настройки и Wi-Fi</h2><form method='POST' action='/save_net'>"));

  String s = "Режим Wi-Fi:<select name='wmode'>";
  s += "<option value='0'" + String(cfg.net.wifiMode == 0 ? " selected" : "") + ">OFF</option>";
  s += "<option value='1'" + String(cfg.net.wifiMode == 1 ? " selected" : "") + ">Клиент (STA)</option>";
  s += "<option value='2'" + String(cfg.net.wifiMode == 2 ? " selected" : "") + ">Точка доступа (AP)</option></select>";
  s += "SSID (до 16 симв):<input name='ssid' maxlength='16' value='" + String(cfg.net.ssid) + "'>";
  s += "Пароль Wi-Fi (до 16):<input name='wpass' maxlength='16' value='" + String(cfg.net.password) + "'>";
  s += "IP режим:<select name='dhcp'><option value='0'" + String(!cfg.net.useStaticIp ? " selected" : "") + ">DHCP</option>";
  s += "<option value='1'" + String(cfg.net.useStaticIp ? " selected" : "") + ">Статический IP</option></select>";
  server.sendContent(s);

  char ipBuf[20];
  s = "";
  snprintf(ipBuf, sizeof(ipBuf), "%d.%d.%d.%d", cfg.net.ip[0], cfg.net.ip[1], cfg.net.ip[2], cfg.net.ip[3]);
  s += "Стат. IP:<input name='ip' value='" + String(ipBuf) + "'>";
  snprintf(ipBuf, sizeof(ipBuf), "%d.%d.%d.%d", cfg.net.gateway[0], cfg.net.gateway[1], cfg.net.gateway[2], cfg.net.gateway[3]);
  s += "Шлюз (Gateway):<input name='gw' value='" + String(ipBuf) + "'>";
  snprintf(ipBuf, sizeof(ipBuf), "%d.%d.%d.%d", cfg.net.subnet[0], cfg.net.subnet[1], cfg.net.subnet[2], cfg.net.subnet[3]);
  s += "Маска (Subnet):<input name='sn' value='" + String(ipBuf) + "'>";
  snprintf(ipBuf, sizeof(ipBuf), "%d.%d.%d.%d", cfg.net.dns[0], cfg.net.dns[1], cfg.net.dns[2], cfg.net.dns[3]);
  s += "DNS:<input name='dns' value='" + String(ipBuf) + "'>";
  s += "Web Логин:<input name='wuser' maxlength='16' value='" + String(cfg.net.webUser) + "'>";
  s += "Web Пароль:<input name='webpass' maxlength='16' value='" + String(cfg.net.webPass) + "'>";
  s += "<button type='submit'>Сохранить сеть</button></form><br><a href='/'>На главную</a></body></html>";
  server.sendContent(s);
}

void handleSaveNet() {
  if (!checkWebAuth()) return;
  bool netChanged = false;
  if (server.hasArg("wmode")) { uint8_t m = server.arg("wmode").toInt(); if (m != cfg.net.wifiMode) { cfg.net.wifiMode = m; netChanged = true; } }
  if (server.hasArg("ssid") && server.arg("ssid") != String(cfg.net.ssid)) { strlcpy(cfg.net.ssid, server.arg("ssid").c_str(), sizeof(cfg.net.ssid)); netChanged = true; }
  if (server.hasArg("wpass") && server.arg("wpass") != String(cfg.net.password)) { strlcpy(cfg.net.password, server.arg("wpass").c_str(), sizeof(cfg.net.password)); netChanged = true; }
  if (server.hasArg("dhcp")) { bool st = (server.arg("dhcp").toInt() == 1); if (st != cfg.net.useStaticIp) { cfg.net.useStaticIp = st; netChanged = true; } }
  if (server.hasArg("ip")) parseIpString(server.arg("ip"), cfg.net.ip);
  if (server.hasArg("gw")) parseIpString(server.arg("gw"), cfg.net.gateway);
  if (server.hasArg("sn")) parseIpString(server.arg("sn"), cfg.net.subnet);
  if (server.hasArg("dns")) parseIpString(server.arg("dns"), cfg.net.dns);
  if (server.hasArg("wuser")) strlcpy(cfg.net.webUser, server.arg("wuser").c_str(), sizeof(cfg.net.webUser));
  if (server.hasArg("webpass")) strlcpy(cfg.net.webPass, server.arg("webpass").c_str(), sizeof(cfg.net.webPass));

  saveConfig();
  server.send(200, "text/html", "<meta charset='utf-8'>Сетевые настройки сохранены! <a href='/net'>Назад</a> | <a href='/'>На главную</a>");
  if (netChanged) { delay(300); applyNetworkSettings(); }
}

void handleSysSettings() {
  if (!checkWebAuth()) return;
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");

  server.sendContent(F("<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>Система</title>"
    "<style>body{font-family:sans-serif;max-width:520px;margin:auto;padding:15px;background:#121212;color:#eee}"
    "input,select{width:100%;padding:6px;margin:4px 0 10px;box-sizing:border-box}"
    "button{padding:10px 16px;background:#2e7d32;color:#fff;border:none;border-radius:4px;cursor:pointer;margin:4px 4px 4px 0}"
    ".warn{background:#c62828}.info{background:#1565c0}.box{border:1px solid #333;padding:10px;border-radius:6px;margin-bottom:14px}"
    "a{color:#64b5f6}</style></head><body><h2>Системные настройки</h2>"));

  String s = "<div class='box'><b>Статус RTC (0x68):</b> " + String(rtcOnline ? "Online" : "Offline") +
             " | <b>Время:</b> " + getFormattedTime() + " | <b>NTP:</b> " + String(ntpSynced ? "Синхр." : "Нет") +
             "<br><form method='POST' action='/save_sys' style='margin-top:8px'>"
             "<button class='info' type='submit' name='act' value='ntp'>Синхронизировать NTP -> RTC</button></form></div>";

  s += "<form method='POST' action='/save_sys'>";
  s += "Часовой пояс (UTC):<input type='number' min='-12' max='14' name='tz' value='" + String(cfg.net.timezone) + "'>";
  s += "Режим подсветки LCD:<select name='blm'>";
  s += "<option value='0'" + String(cfg.backlightMode == 0 ? " selected" : "") + ">Всегда ВКЛ</option>";
  s += "<option value='1'" + String(cfg.backlightMode == 1 ? " selected" : "") + ">Всегда ВЫКЛ</option>";
  s += "<option value='2'" + String(cfg.backlightMode == 2 ? " selected" : "") + ">Автотаймер</option></select>";
  s += "Таймаут подсветки (сек):<input type='number' min='5' max='3600' name='blt' value='" + String(cfg.backlightTimeout) + "'>";
  s += "Период опроса датчиков (сек):<input type='number' min='1' max='3600' name='poll' value='" + String(cfg.pollIntervalSec) + "'>";
  s += "Угол сервопривода (0..180):<input type='number' min='0' max='180' name='servo' value='" + String(cfg.servoAngle) + "'>";
  s += "Контроль 24V PCF слот (0=выкл, 1..8):<input type='number' min='0' max='8' name='psdev' value='" + String(cfg.powerSensePcfIdx == 255 ? 0 : cfg.powerSensePcfIdx + 1) + "'>";
  s += "Контроль 24V PCF пин (0..7):<input type='number' min='0' max='7' name='psbit' value='" + String(cfg.powerSensePcfBit) + "'>";
  s += "<button type='submit' name='act' value='save'>Сохранить настройки</button></form>";

  s += "<div class='box' style='margin-top:16px'><b>Обслуживание и питание</b><br><br>"
     "<form method='POST' action='/recover_i2c' style='display:inline'>"
     "<button class='info' type='submit'>Восстановить LCD и I2C</button></form> "
     "<form method='POST' action='/reboot' style='display:inline' onsubmit=\"return confirm('Вы действительно хотите перезагрузить метеостанцию ESP-07?');\">"
     "<button class='warn' type='submit'>Перезагрузить устройство</button></form></div>"
     "<a href='/'>На главную</a></body></html>";
  server.sendContent(s);
}

void handleSaveSys() {
  if (!checkWebAuth()) return;
  if (server.hasArg("act") && server.arg("act") == "ntp") {
    initRtc();
    syncRtcWithNtp();
    server.send(200, "text/html", "<meta charset='utf-8'>Запрос синхронизации NTP -> RTC выполнен! <a href='/sys'>Назад</a>");
    return;
  }
  if (server.hasArg("tz")) {
    cfg.net.timezone = constrain(server.arg("tz").toInt(), -12, 14);
    configTime((long)cfg.net.timezone * 3600L, 0, "pool.ntp.org", "time.nist.gov");
  }
  if (server.hasArg("blm")) cfg.backlightMode = constrain(server.arg("blm").toInt(), 0, 2);
  if (server.hasArg("blt")) cfg.backlightTimeout = constrain(server.arg("blt").toInt(), 5, 3600);
  if (server.hasArg("poll")) cfg.pollIntervalSec = constrain(server.arg("poll").toInt(), 1, 3600);
  if (server.hasArg("servo")) setServoAngle(constrain(server.arg("servo").toInt(), 0, 180));
  if (server.hasArg("psdev")) {
    int v = server.arg("psdev").toInt();
    cfg.powerSensePcfIdx = (v <= 0) ? 255 : constrain(v - 1, 0, MAX_I2C_DEVICES - 1);
  }
  if (server.hasArg("psbit")) cfg.powerSensePcfBit = constrain(server.arg("psbit").toInt(), 0, 7);

  saveConfig();
  updateBacklight();
  server.send(200, "text/html", "<meta charset='utf-8'>Системные настройки сохранены! <a href='/sys'>Назад</a> | <a href='/'>На главную</a>");
}

void handleRecoverI2c() {
  if (!checkWebAuth()) return;
  recoverI2cBusAndDisplay();
  server.send(200, "text/html", "<meta charset='utf-8'>Шина I2C и дисплей LCD2004 перезапущены! <a href='/sys'>Назад</a> | <a href='/'>На главную</a>");
}

void handleReboot() {
  if (!checkWebAuth()) return;
  server.sendHeader("Connection", "close");
  server.send(200, "text/html", "<meta charset='utf-8'><meta http-equiv='refresh' content='10;url=/'>Перезагрузка ESP-07... Обновление через 10 сек.");
  delay(300);
//  ESP.reset(); // Аппаратный программный сброс ядра работает надёжнее ESP.restart() после UART-прошивки
  ESP.restart();
}


String renderFmtHtml(const DisplayFormat &f, uint8_t pCount, const char *p1, const char *p2, const char *p3) {
  String h = "<label><input type='checkbox' name='en'" + String(f.enabled ? " checked" : "") + "> Включен</label> ";
  h += "<label><input type='checkbox' name='main'" + String(f.showOnMain ? " checked" : "") + "> На гл. экране</label> ";
  h += "<label><input type='checkbox' name='snm'" + String(f.showName ? " checked" : "") + "> Показ. имя</label><br>";
  h += "Имя (до 8): <input name='name' size='8' maxlength='8' value='" + String(f.name) + "'> ";
  h += "Строка (-1..3): <input type='number' min='-1' max='3' name='row' style='width:52px' value='" + String(f.row) + "'> ";
  h += "Столбец (-1..19): <input type='number' min='-1' max='19' name='col' style='width:52px' value='" + String(f.col) + "'><br>";
  h += "<label><input type='checkbox' name='p1'" + String(f.showP1 ? " checked" : "") + "> " + String(p1) + "</label> ";
  if (pCount >= 2) h += "<label><input type='checkbox' name='p2'" + String(f.showP2 ? " checked" : "") + "> " + String(p2) + "</label> ";
  if (pCount >= 3) h += "<label><input type='checkbox' name='p3'" + String(f.showP3 ? " checked" : "") + "> " + String(p3) + "</label> ";
  h += "<label><input type='checkbox' name='dec'" + String(f.showDecimals ? " checked" : "") + "> Дробные</label> ";
  h += "<label><input type='checkbox' name='pls'" + String(f.showPlusSign ? " checked" : "") + "> Знак +</label> ";
  h += "<label><input type='checkbox' name='unt'" + String(f.showUnit ? " checked" : "") + "> Ед.изм.</label><br>";
  return h;
}

void applyFmtFromArgs(DisplayFormat &f) {
  f.enabled = server.hasArg("en");
  f.showOnMain = server.hasArg("main");
  f.showName = server.hasArg("snm");
  if (server.hasArg("name")) strlcpy(f.name, server.arg("name").c_str(), sizeof(f.name));
  if (server.hasArg("row")) f.row = constrain(server.arg("row").toInt(), -1, 3);
  if (server.hasArg("col")) f.col = constrain(server.arg("col").toInt(), -1, 19);
  f.showP1 = server.hasArg("p1");
  f.showP2 = server.hasArg("p2");
  f.showP3 = server.hasArg("p3");
  f.showDecimals = server.hasArg("dec");
  f.showPlusSign = server.hasArg("pls");
  f.showUnit = server.hasArg("unt");
}

void handleSensorsPage() {
  if (!checkWebAuth()) return;
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");

  server.sendContent(F("<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>Датчики</title>"
    "<style>body{font-family:sans-serif;max-width:720px;margin:auto;padding:12px;background:#121212;color:#eee}"
    ".card{border:1px solid #333;padding:10px;margin-bottom:10px;border-radius:6px;background:#1a1a1a}"
    "input,select{padding:4px;margin:2px}button{padding:7px 12px;background:#2e7d32;color:#fff;border:none;border-radius:4px;cursor:pointer}"
    ".scan{background:#1565c0;margin-right:6px}.del{background:#c62828}a{color:#64b5f6}</style></head><body>"
    "<a href='/' style='margin-left:10px'>На главную</a><br>"
    "<h2>Управление датчиками</h2>"
    "<form method='POST' action='/scan_sensors' style='margin-bottom:14px'>"
    "<button class='scan' type='submit' name='bus' value='ow'>Сканировать 1-Wire</button>"
    "<button class='scan' type='submit' name='bus' value='i2c'>Сканировать I2C</button>"
    "</form>"));

  String ignStr = "";
  for (int i = 0; i < 8; i++) {
    if (cfg.i2cIgnoreAddrs[i] != 0) {
      if (ignStr.length() > 0) ignStr += ", ";
      ignStr += "0x";
      if (cfg.i2cIgnoreAddrs[i] < 0x10) ignStr += "0";
      ignStr += String(cfg.i2cIgnoreAddrs[i], HEX);
    }
  }
  ignStr.toUpperCase();
  ignStr.replace("0X", "0x");

  server.sendContent("<div class='card'><form method='POST' action='/save_i2c_ignore'>"
    "<b>Исключения сканирования I2C</b> (HEX через запятую, например: <code>0x50, 0x57</code>; 0x27 и 0x68 скрыты всегда):<br>"
    "<input name='ign' style='width:70%' value='" + ignStr + "'> "
    "<button type='submit'>Сохранить список</button></form></div>");

  // 1. DHT22
  server.sendContent(F("<h3>Датчик DHT22 (GPIO2)</h3><div class='card'><form method='POST' action='/save_sensor'>"
    "<input type='hidden' name='kind' value='dht'><input type='hidden' name='idx' value='0'>"));
  String h = "Режим: <select name='dmode'>";
  h += "<option value='0'" + String(cfg.dhtSensors[0].mode == 0 ? " selected" : "") + ">Темп + Влажн</option>";
  h += "<option value='1'" + String(cfg.dhtSensors[0].mode == 1 ? " selected" : "") + ">Только Темп</option>";
  h += "<option value='2'" + String(cfg.dhtSensors[0].mode == 2 ? " selected" : "") + ">Только Влажн</option></select><br>";
  h += renderFmtHtml(cfg.dhtSensors[0].fmt, 2, "Темп", "Влажн", "");
  h += "<button type='submit'>Сохранить DHT</button></form></div>";
  server.sendContent(h);

  // 2. 1-Wire DS18B20
  server.sendContent(F("<h3>Датчики 1-Wire (DS18B20)</h3>"));
  for (int i = 0; i < MAX_OW_SENSORS; i++) {
    if (cfg.owSensors[i].addr[0] == 0) continue;
    char idBuf[17];
    for (int b = 0; b < 8; b++) snprintf(idBuf + b * 2, 3, "%02X", cfg.owSensors[i].addr[b]);
    h = "<div class='card'><form method='POST' action='/save_sensor'>";
    h += "<input type='hidden' name='kind' value='ow'><input type='hidden' name='idx' value='" + String(i) + "'>";
    h += "<b>Слот #" + String(i + 1) + "</b> | ID: <code>" + String(idBuf) + "</code> | Текущ: <b>" +
         (isnan(owValues[i]) ? "ERR" : String(owValues[i], 1) + " C") + "</b><br>";
    h += renderFmtHtml(cfg.owSensors[i].fmt, 1, "Темп", "", "");
    h += "<button type='submit' name='act' value='save'>Сохранить</button> ";
    h += "<button class='del' type='submit' name='act' value='del' onclick=\"return confirm('Удалить датчик?');\">Удалить</button></form></div>";
    server.sendContent(h);
  }

  // 3. I2C устройства
  server.sendContent(F("<h3>I2C устройства</h3>"));
  const char* tNames[] = {"None", "BMP180", "BME280", "BMP280", "AHT20", "SHT41", "BH1750", "PCF8574"};
  for (int i = 0; i < MAX_I2C_DEVICES; i++) {
    if (cfg.i2cDevices[i].addr == 0) continue;
    char hexBuf[6];
    snprintf(hexBuf, sizeof(hexBuf), "0x%02X", cfg.i2cDevices[i].addr);
    h = "<div class='card'><form method='POST' action='/save_sensor'>";
    h += "<input type='hidden' name='kind' value='i2c'><input type='hidden' name='idx' value='" + String(i) + "'>";
    h += "<b>Слот #" + String(i + 1) + "</b> | Адрес: <code>" + String(hexBuf) + "</code> | Тип: <select name='itype'>";
    for (int t = 0; t < 8; t++)
      h += "<option value='" + String(t) + "'" + String(cfg.i2cDevices[i].type == t ? " selected" : "") + ">" + String(tNames[t]) + "</option>";
    h += "</select><br>";
    h += renderFmtHtml(cfg.i2cDevices[i].fmt, 3, "P1", "P2", "P3");

    if (cfg.i2cDevices[i].type == I2C_PCF8574) {
      h += "<div style='margin:6px 0;padding:6px;border:1px dashed #444'><b>Каналы PCF8574:</b><br>";
      for (int p = 0; p < 8; p++) {
        bool isOut = (cfg.i2cDevices[i].pcfDirMask >> p) & 1;
        bool isInv = (cfg.i2cDevices[i].pcfInvMask >> p) & 1;
        h += "P" + String(p) + ": <input name='pn" + String(p) + "' size='6' maxlength='6' value='" + String(cfg.i2cDevices[i].pcfPinNames[p]) + "'> ";
        h += "<select name='pd" + String(p) + "'><option value='1'" + String(isOut ? " selected" : "") + ">OUT</option>";
        h += "<option value='0'" + String(!isOut ? " selected" : "") + ">IN</option></select> ";
        h += "<label><input type='checkbox' name='pi" + String(p) + "'" + String(isInv ? " checked" : "") + "> Инверт.</label><br>";
      }
      h += "</div>";
    }
    h += "<button type='submit' name='act' value='save'>Сохранить</button> ";
    h += "<button class='del' type='submit' name='act' value='del' onclick=\"return confirm('Удалить I2C устройство?');\">Удалить</button></form></div>";
    server.sendContent(h);
  }
  server.sendContent(F("<br><a href='/'>На главную</a></body></html>"));
}

void handleSaveI2cIgnore() {
  if (!checkWebAuth()) return;
  memset(cfg.i2cIgnoreAddrs, 0, sizeof(cfg.i2cIgnoreAddrs));
  if (server.hasArg("ign")) {
    String s = server.arg("ign");
    int slot = 0, pos = 0;
    while (pos < (int)s.length() && slot < 8) {
      int comma = s.indexOf(',', pos);
      String tok = (comma == -1) ? s.substring(pos) : s.substring(pos, comma);
      tok.trim();
      if (tok.length() > 0) {
        long val = strtol(tok.c_str(), NULL, 0); // Понимает и "0x50", и десятичные числа
        if (val > 0 && val < 127) cfg.i2cIgnoreAddrs[slot++] = (uint8_t)val;
      }
      if (comma == -1) break;
      pos = comma + 1;
    }
  }
  saveConfig();
  server.send(200, "text/html", "<meta charset='utf-8'>Список исключений I2C сохранён! <a href='/sensors'>Назад к датчикам</a>");
}

void handleScanSensors() {
  if (!checkWebAuth()) return;
  String bus = server.arg("bus");
  uint8_t found = 0;

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");
  server.sendContent(F("<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<style>body{font-family:sans-serif;max-width:650px;margin:auto;padding:15px;background:#121212;color:#eee}"
    "table{width:100%;border-collapse:collapse;margin:12px 0}td,th{border:1px solid #333;padding:8px;text-align:left}"
    "a.btn{display:inline-block;padding:8px 14px;background:#1976d2;color:#fff;text-decoration:none;border-radius:4px}</style>"
    "</head><body><h2>Результат сканирования</h2>"));

  if (bus == "ow") {
    server.sendContent(F("<table><thead><tr><th>Слот</th><th>ID датчика (ROM)</th><th>Температура</th><th>Статус</th></tr></thead><tbody>"));
    owSensorsBus.begin();
    delay(20);
    uint64_t addrStorage = 0;
    uint8_t *addr = (uint8_t*)&addrStorage;

    for (uint8_t attempt = 0; attempt < 3 && found == 0; attempt++) {
      oneWire.reset_search();
      delay(10);
      while (oneWire.search(addr)) {
        yield();
        if (OneWire::crc8(addr, 7) != *(addr + 7)) continue;
        bool isNew = false;
        int slot = -1;
        for (int i = 0; i < MAX_OW_SENSORS; i++) {
          if (memcmp(cfg.owSensors[i].addr, addr, 8) == 0) slot = i;
        }
        if (slot == -1) {
          for (int i = 0; i < MAX_OW_SENSORS; i++) {
            if (cfg.owSensors[i].addr[0] == 0) {
              slot = i;
              memcpy(cfg.owSensors[i].addr, addr, 8);
              cfg.owSensors[i].fmt.enabled = true;
              cfg.owSensors[i].fmt.showOnMain = true;
              isNew = true;
              break;
            }
          }
        }
        String idStr = "";
        for (int b = 0; b < 8; b++) {
          uint8_t v = *(addr + b);
          if (v < 0x10) idStr += "0";
          idStr += String(v, HEX);
        }
        idStr.toUpperCase();

        String tempStr = "—";
        if (slot != -1) {
          owSensorsBus.requestTemperaturesByAddress(addr);
          delay(750);
          owValues[slot] = owSensorsBus.getTempC(addr);
          tempStr = isnan(owValues[slot]) ? "ERR" : (String(owValues[slot], 2) + " °C");
        }
        String row = "<tr><td>" + (slot != -1 ? ("#" + String(slot + 1)) : "Нет мест") + "</td>"
                     "<td><code>" + idStr + "</code></td>"
                     "<td><b>" + tempStr + "</b></td>"
                     "<td>" + (isNew ? "Добавлен" : "Уже в списке") + "</td></tr>";
        server.sendContent(row);
        found++;
      }
    }
    server.sendContent(F("</tbody></table>"));
  } else if (bus == "i2c") {
    const char* tNames[] = {"None", "BMP180", "BME280", "BMP280", "AHT20", "SHT41", "BH1750", "PCF8574"};
    server.sendContent(F("<table><thead><tr><th>Слот</th><th>Адрес</th><th>Определён как</th><th>Статус</th></tr></thead><tbody>"));
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

        bool isNew = false;
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
              isNew = true;
              break;
            }
          }
        }
        String hexStr = "0x";
        if (addr < 0x10) hexStr += "0";
        String hVal = String(addr, HEX);
        hVal.toUpperCase();
        hexStr += hVal;

        uint8_t curType = (slot != -1) ? cfg.i2cDevices[slot].type : det;
        String row = "<tr><td>" + (slot != -1 ? ("#" + String(slot + 1)) : "Нет мест") + "</td>"
                     "<td><code>" + hexStr + "</code></td>"
                     "<td><b>" + String(tNames[curType]) + "</b></td>"
                     "<td>" + (isNew ? "Добавлен" : "Уже в списке") + "</td></tr>";
        server.sendContent(row);
        found++;
      }
    }
    server.sendContent(F("</tbody></table>"));
  }

  saveConfig();
  initPeripherals();
  server.sendContent("<p>Всего найдено: <b>" + String(found) + "</b></p><a class='btn' href='/sensors'>Вернуться к настройке датчиков</a></body></html>");
}

void handleSaveSensor() {
  if (!checkWebAuth()) return;
  String kind = server.arg("kind");
  int idx = server.arg("idx").toInt();
  String act = server.arg("act");

  if (kind == "dht" && idx >= 0 && idx < MAX_DHT_SENSORS) {
    if (server.hasArg("dmode")) cfg.dhtSensors[idx].mode = constrain(server.arg("dmode").toInt(), 0, 2);
    applyFmtFromArgs(cfg.dhtSensors[idx].fmt);
  } else if (kind == "ow" && idx >= 0 && idx < MAX_OW_SENSORS) {
    if (act == "del") {
      memset(cfg.owSensors[idx].addr, 0, 8);
      cfg.owSensors[idx].fmt.enabled = false;
    } else {
        applyFmtFromArgs(cfg.owSensors[idx].fmt);
    }
    } else if (kind == "i2c" && idx >= 0 && idx < MAX_I2C_DEVICES) {
        if (act == "del") {
          cfg.i2cDevices[idx].addr = 0;
          cfg.i2cDevices[idx].type = I2C_NONE;
          cfg.i2cDevices[idx].fmt.enabled = false;
        } else {
            if (server.hasArg("itype")) cfg.i2cDevices[idx].type = (I2cDevType)constrain(server.arg("itype").toInt(), 0, 7);
            applyFmtFromArgs(cfg.i2cDevices[idx].fmt);
            if (cfg.i2cDevices[idx].type == I2C_PCF8574) {
              for (int p = 0; p < 8; p++) {
                if (server.hasArg("pn" + String(p))) strlcpy(cfg.i2cDevices[idx].pcfPinNames[p], server.arg("pn" + String(p)).c_str(), 7);
                if (server.arg("pd" + String(p)).toInt() == 1) cfg.i2cDevices[idx].pcfDirMask |= (1 << p);
                else cfg.i2cDevices[idx].pcfDirMask &= ~(1 << p);
                if (server.hasArg("pi" + String(p))) cfg.i2cDevices[idx].pcfInvMask |= (1 << p);
                else cfg.i2cDevices[idx].pcfInvMask &= ~(1 << p);
              }
            applyPcfOutputs(idx);
            }
          }
    }
  saveConfig();
  initPeripherals();
  server.send(200, "text/html", "<meta charset='utf-8'>Настройки датчика сохранены! <a href='/sensors'>Назад к датчикам</a> | <a href='/'>На главную</a>");
}

void initWebServer() {
server.on("/", handleRoot);
server.on("/api/all", handleApiAll);
server.on("/api/sensor", handleApiSensor);
server.on("/api/relay", handleApiRelay);
server.on("/rules", handleRules);
server.on("/save_rules", HTTP_POST, handleSaveRules);
//server.on("/settings", handleSettings);
//server.on("/save", HTTP_POST, handleSave);
server.on("/net", handleNetSettings);
server.on("/save_net", HTTP_POST, handleSaveNet);
server.on("/sys", handleSysSettings);
server.on("/save_sys", HTTP_POST, handleSaveSys);
server.on("/recover_i2c", HTTP_POST, handleRecoverI2c);
server.on("/reboot", HTTP_POST, handleReboot);
server.on("/sensors", handleSensorsPage);
server.on("/scan_sensors", HTTP_POST, handleScanSensors);
server.on("/save_sensor", HTTP_POST, handleSaveSensor);
server.on("/save_i2c_ignore", HTTP_POST, handleSaveI2cIgnore);
server.begin();
}
