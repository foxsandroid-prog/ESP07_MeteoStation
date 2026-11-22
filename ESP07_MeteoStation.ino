#include "Config.h"
#include "Sensors.h"
#include "WebServer.h"
#include "DisplayMenu.h"

bool lastBtnReading = HIGH;
bool btnStableState = HIGH;
unsigned long lastBtnDebounceTime = 0;
bool btnPressedFlag = false;
unsigned long lastNtpCheck = 0;
unsigned long lastClockDraw = 0;

bool checkMainScreenButtonClick() {
  bool reading = digitalRead(pinSW);
  if (reading != lastBtnReading) lastBtnDebounceTime = millis();
  if ((millis() - lastBtnDebounceTime) > 35) {
    if (reading != btnStableState) {
      btnStableState = reading;
      if (btnStableState == LOW) btnPressedFlag = true;
      else if (btnStableState == HIGH && btnPressedFlag) {
        btnPressedFlag = false;
        lastBtnReading = reading;
        return true;
      }
    }
  }
  lastBtnReading = reading;
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println();
  Serial.print("Reset reason: ");
  Serial.println(ESP.getResetReason());
  
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

  // 3. Проверка нажатия кнопки энкодера для входа в меню
  if (checkMainScreenButtonClick()) {
    bool wasOff = !isBacklightOn;
    registerUserActivity();
    if (!wasOff) {
      openMainMenu();
      return;
    }
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
    updatePowerStatus();
    drawMainScreen();
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
