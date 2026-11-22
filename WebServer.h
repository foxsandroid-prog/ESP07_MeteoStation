#pragma once
#include <ESP8266WebServer.h>
#include "Config.h"
#include "Sensors.h"

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
<a class='btn' href='/rules'>Правила автоматизации</a>
<a class='btn' href='/settings'>Настройки (Пароль)</a>
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
  html += "Пароль Wi-Fi (до 16):<input name='wpass' maxlength='16' value='" + String(cfg.net.password) + "'>";
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

void initWebServer() {
server.on("/", handleRoot);
server.on("/api/all", handleApiAll);
server.on("/api/sensor", handleApiSensor);
server.on("/api/relay", handleApiRelay);
server.on("/rules", handleRules);
server.on("/save_rules", HTTP_POST, handleSaveRules);
server.on("/settings", handleSettings);
server.on("/save", HTTP_POST, handleSave);
server.begin();
}
