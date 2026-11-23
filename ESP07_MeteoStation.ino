#include "Config.h"
#include "Sensors.h"
#include "DisplayMenu.h"
#include "WebServer.h"

bool lastBtnReading = HIGH;
bool btnStableState = HIGH;
unsigned long lastBtnDebounceTime = 0;
bool btnPressedFlag = false;
unsigned long lastNtpCheck = 0;
unsigned long lastClockDraw = 0;

unsigned long btnPressStart = 0;
bool longPressHandled = false;

// Возвращает: 0 = нет события, 1 = короткий клик, 2 = длинное удержание (>= 1.5 сек)
uint8_t checkMainScreenButtonEvent() {
  bool reading = digitalRead(pinSW);
  if (reading != lastBtnReading) lastBtnDebounceTime = millis();

  if ((millis() - lastBtnDebounceTime) > 35) {
    if (reading != btnStableState) {
      btnStableState = reading;
      if (btnStableState == LOW) {
        btnPressedFlag = true;
        btnPressStart = millis();
        longPressHandled = false;
      } else if (btnStableState == HIGH && btnPressedFlag) {
        btnPressedFlag = false;
        lastBtnReading = reading;
        if (!longPressHandled) return 1; // Короткий клик при отпускании
      }
    }
  }

  // Срабатывание длинного нажатия прямо во время удержания (не дожидаясь отпускания)
  if (btnStableState == LOW && btnPressedFlag && !longPressHandled && (millis() - btnPressStart >= 1500)) {
    longPressHandled = true;
    lastBtnReading = reading;
    return 2;
  }

  lastBtnReading = reading;
  return 0;
}

void recoverI2cBusAndDisplay() {
  // 1. 9 тактов SCL для освобождения SDA
  pinMode(pinSDA, INPUT_PULLUP);
  pinMode(pinSCL, OUTPUT);
  for (uint8_t i = 0; i < 9; i++) {
    digitalWrite(pinSCL, LOW);
    delayMicroseconds(10);
    digitalWrite(pinSCL, HIGH);
    delayMicroseconds(10);
  }
  pinMode(pinSDA, OUTPUT);
  digitalWrite(pinSDA, LOW);
  delayMicroseconds(10);
  digitalWrite(pinSCL, HIGH);
  delayMicroseconds(10);
  digitalWrite(pinSDA, HIGH);
  delayMicroseconds(10);

  // 2. Инициализируем LCD, а затем ОБЯЗАТЕЛЬНО заново задаём пины и частоту Wire!
  lcd.begin();
  Wire.begin(pinSDA, pinSCL);
  Wire.setClock(50000);

  lcd.createChar(7, degreeChar);
  updateBacklight();
  initRtc();
  initPeripherals();
  drawMainScreen();
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println();
  Serial.print("Reset reason: ");
  Serial.println(ESP.getResetReason());

  Serial.print("Real Flash Size: ");
  Serial.println(ESP.getFlashChipRealSize());
  Serial.print("IDE Flash Size: ");
  Serial.println(ESP.getFlashChipSize());
  Serial.print("Config bytes: ");
  Serial.println(sizeof(SystemConfig));
  
  // Явно задаём пины I2C для ESP-07 (SDA = GPIO4, SCL = GPIO5)
  Wire.begin(pinSDA, pinSCL);
  loadConfig();

  // Настройка пинов энкодера (GPIO15 подтянут к GND аппаратно, поэтому INPUT без PULLUP)
  pinMode(pinCLK, INPUT);
  pinMode(pinDT, INPUT);
  pinMode(pinSW, INPUT);

  initRtc();
  initPeripherals();

  lcd.begin();
  Wire.begin(pinSDA, pinSCL);
  Wire.setClock(50000);  
  lcd.createChar(7, degreeChar);
  lcd.home();
  lcd.clear();
  lcd.attachEncoder(pinCLK, pinDT, pinSW);
  lcd.attachIdleFunc([]() {
    server.handleClient();
    yield();
  });

  updateBacklight();
  registerUserActivity();

  lcd.setCursor(2, 1);
  lcd.print("System Starting");

  applyNetworkSettings();
  initWebServer();

  runBlockingPollAll();
  lcd.clear();
  drawMainScreen();
  lastPollCycleStart = millis();
}

void loop() {
  // 1. Обслуживание веб-сервера и приёмника 433 МГц в реальном времени
  server.handleClient();
  checkRfReceiver();
  updateBacklight();

  unsigned long now = millis();

  // 2. Периодическая синхронизация RTC по NTP (раз в час или при первом подключении)
  if ((!ntpSynced && now > 10000) || (now - lastNtpCheck >= 3600000UL)) {
    lastNtpCheck = now;
    syncRtcWithNtp();
  }
  
  // 3. Проверка нажатия (короткий клик -> меню, долгое удержание 1.5с -> сброс I2C/LCD)
  uint8_t btnEv = checkMainScreenButtonEvent();
  if (btnEv == 1) {
    bool wasOff = !isBacklightOn;
    registerUserActivity();
    if (!wasOff) {
      openMainMenu();
      return;
    }
  } else if (btnEv == 2) {
    registerUserActivity();
    recoverI2cBusAndDisplay();
    return;
  }

  // 4. Прокрутка главного экрана энкодером
  eEncoderState state = lcd.getEncoderState();
  if (state == eLeft || state == eRight) {
    bool wasOff = !isBacklightOn;
    registerUserActivity();
    if (!wasOff) {
      if (state == eLeft) mainScrollOffset--;
      else mainScrollOffset++;
      drawMainScreen();
    }
  }

  // 5. Обновление часов и напряжения АКБ на экране раз в 2 секунды
  if (now - lastClockDraw >= 2000) {
    lastClockDraw = now;
    Wire.beginTransmission(LCD_ADDR);
    bool lcdAlive = (Wire.endTransmission() == 0);
    bool anyI2cOffline = false;
    for (uint8_t i = 0; i < MAX_I2C_DEVICES; i++) {
      if (cfg.i2cDevices[i].fmt.enabled && !i2cValues[i].online) {
        anyI2cOffline = true;
        break;
      }
    }
    if (!lcdAlive || anyI2cOffline) {
      recoverI2cBusAndDisplay();
    } else {
      updatePowerStatus();
      drawMainScreen();
    }
  }

  if (digitalRead(pinSW) == LOW) return;

  // 6. Неблокирующий конечный автомат опроса датчиков
  switch (pollPhase) {
    case POLL_IDLE:
      if (now - lastPollCycleStart >= (unsigned long)cfg.pollIntervalSec * 1000UL) {
        lastPollCycleStart = now;
        startOneWireConversion();
      }
      break;

    case POLL_OW_WAIT:
      if (now - owConvertStart >= 780) {
        finishOneWireConversion();
        drawMainScreen();
        stepIndex = 0;
        pollPhase = POLL_DHT_STEP;
      }
      break;

    case POLL_DHT_STEP:
      if (stepIndex < MAX_DHT_SENSORS) {
        readSingleDht(stepIndex);
        stepIndex++;
      } else {
        drawMainScreen();
        stepIndex = 0;
        pollPhase = POLL_I2C_STEP;
      }
      break;

    case POLL_I2C_STEP:
      if (stepIndex < MAX_I2C_DEVICES) {
        readSingleI2c(stepIndex);
        stepIndex++;
      } else {
        evaluateAutomationRules();
        drawMainScreen();
        pollPhase = POLL_IDLE;
      }
      break;
  }
}
