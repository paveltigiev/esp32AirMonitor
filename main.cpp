#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <SparkFun_SCD4x_Arduino_Library.h>
#include <Preferences.h>  // добавили для Flash

// ==========================
// Настройки OLED дисплея
// ==========================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ==========================
// Пины ESP32-C3 SuperMini
// ==========================
#define SDA_PIN 9   // SDA для OLED и SCD40
#define SCL_PIN 8   // SCL для OLED и SCD40
#define CALIBRATION_BUTTON_PIN 4 // кнопка калибровки

// ==========================
// Объект для датчика
// ==========================
SCD4x scd4x;
Preferences preferences;   // объект для хранения калибровки

// ==========================
// Переменные для калибровки
// ==========================
bool calibrationInProgress = false;
unsigned long calibrationStartTime = 0;
const unsigned long CALIBRATION_WAIT_TIME = 180000; // 3 минуты
int16_t lastCalibrationOffset = -1; // сохраняем смещение

// ==========================
// Переменные для тренда
// ==========================
int trendArrow = 0; // 0=stable, 1=up, 2=down
uint16_t co2History[3] = {0};
int historyIndex = 0;

// ==========================
// SETUP
// ==========================
void setup() {
  Serial.begin(115200);
  delay(3000); // задержка для стабилизации питания

  pinMode(CALIBRATION_BUTTON_PIN, INPUT_PULLUP);

  // I2C для дисплея и датчика
  Wire.begin(SDA_PIN, SCL_PIN);

  // Инициализация дисплея
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;);
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  display.println("Init OLED OK");
  display.display();
  delay(1000);

  // Инициализация датчика
  display.clearDisplay();
  display.setCursor(0,0);
  display.println("Init SCD40...");
  display.display();

  if (!scd4x.begin(Wire)) {
    Serial.println("SCD40 not detected!");
    display.clearDisplay();
    display.setCursor(0,0);
    display.println("SCD40 ERROR!");
    display.display();
    while (1); // стоп, если датчик не найден
  }

  Serial.println("SCD40 initialized");

  // Отключаем авто-калибровку
  scd4x.stopPeriodicMeasurement();
  delay(500);
  scd4x.setAutomaticSelfCalibrationEnabled(false);
  scd4x.startPeriodicMeasurement();

  // Загружаем последнюю калибровку из Flash
  preferences.begin("co2calib", true);
  lastCalibrationOffset = preferences.getInt("offset", -1);
  preferences.end();

  if (lastCalibrationOffset != -1) {
    Serial.print("Restoring calibration offset: ");
    Serial.println(lastCalibrationOffset);

    scd4x.stopPeriodicMeasurement();
    delay(500);
    scd4x.performForcedRecalibration(lastCalibrationOffset);
    delay(500);
    scd4x.startPeriodicMeasurement();
  }

  display.clearDisplay();
  display.setCursor(0,0);
  display.println("CO2 Monitor Ready");
  display.println("Manual Cal Only");
  display.println("Warming up...");
  display.display();
  delay(5000);
}

// ==========================
// LOOP
// ==========================
void loop() {
  static bool buttonPressed = false;
  bool currentButtonState = digitalRead(CALIBRATION_BUTTON_PIN);

  if (currentButtonState == LOW && !buttonPressed && !calibrationInProgress) {
    buttonPressed = true;
  } else if (currentButtonState == HIGH && buttonPressed && !calibrationInProgress) {
    buttonPressed = false;
    startManualCalibration();
  }

  if (calibrationInProgress) {
    handleCalibrationProcess();
    return;
  }

  if (scd4x.readMeasurement()) {
    if (scd4x.getCO2() > 0) {
      uint16_t co2 = scd4x.getCO2();
      float temperature = scd4x.getTemperature();
      float humidity = scd4x.getHumidity();

      updateTrend(co2);
      displayMeasurements(co2, temperature, humidity);

      Serial.print("CO2: "); Serial.print(co2);
      Serial.print(" ppm, T: "); Serial.print(temperature);
      Serial.print(" C, H: "); Serial.print(humidity);
      Serial.println(" %");
    }
  }

  delay(5000);
}

// ==========================
// ФУНКЦИИ
// ==========================
void updateTrend(uint16_t currentCO2) {
  co2History[historyIndex] = currentCO2;

  if (historyIndex > 0) {
    int diff = currentCO2 - co2History[historyIndex - 1];
    if (diff > 50) trendArrow = 1;
    else if (diff < -50) trendArrow = 2;
    else trendArrow = 0;
  }

  historyIndex = (historyIndex + 1) % 3;
}

void displayMeasurements(uint16_t co2, float temperature, float humidity) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("CO2 Monitor");

  display.setCursor(100, 0);
  if (trendArrow == 1) display.print("UP");
  else if (trendArrow == 2) display.print("DOWN");
  else display.print("OK");

  display.drawLine(0, 10, SCREEN_WIDTH, 10, SSD1306_WHITE);

  display.setTextSize(3);
  display.setCursor(0, 15);
  display.print(co2);

  display.setTextSize(1);
  display.setCursor(85, 32);
  display.print("ppm");

  display.setCursor(0, 42);
  if (co2 < 400) display.print("EXCELLENT");
  else if (co2 < 1000) display.print("GOOD");
  else if (co2 < 2000) display.print("OK");
  else if (co2 < 5000) display.print("POOR");
  else display.print("DANGER");

  display.setCursor(0, 54);
  display.print("T: "); display.print(temperature, 1); display.print("C");

  display.setCursor(65, 54);
  display.print("H: "); display.print(humidity, 1); display.print("%");

  display.display();
}

// ==========================
// КАЛИБРОВКА
// ==========================
void startManualCalibration() {
  calibrationInProgress = true;
  calibrationStartTime = millis();

  scd4x.stopPeriodicMeasurement();
  delay(500);
  scd4x.startPeriodicMeasurement();

  display.clearDisplay();
  display.setCursor(0,0);
  display.println("CALIBRATION MODE");
  display.display();
  delay(2000);
}

void handleCalibrationProcess() {
  unsigned long elapsedTime = millis() - calibrationStartTime;
  unsigned long remainingTime = CALIBRATION_WAIT_TIME - elapsedTime;

  if (elapsedTime < CALIBRATION_WAIT_TIME) {
    int minutes = remainingTime / 60000;
    int seconds = (remainingTime % 60000) / 1000;

    display.clearDisplay();
    display.setCursor(0,0);
    display.println("CALIBRATING...");
    display.print("Time left: ");
    display.print(minutes);
    display.print(":");
    if (seconds < 10) display.print("0");
    display.println(seconds);
    display.display();
    delay(1000);
  } else {
    performManualCalibration();
  }
}

void performManualCalibration() {
  scd4x.stopPeriodicMeasurement();
  delay(500);

  int16_t correction = scd4x.performForcedRecalibration(400);

  delay(500);
  scd4x.startPeriodicMeasurement();

  display.clearDisplay();
  if (correction != 0xFFFF) {
    display.println("CALIBRATION OK");
    display.print("Offset: ");
    display.println(correction);

    // Сохраняем в Flash
    preferences.begin("co2calib", false);
    preferences.putInt("offset", correction);
    preferences.end();

    lastCalibrationOffset = correction;

  } else {
    display.println("CALIBRATION FAIL");
  }
  display.display();
  delay(5000);

  calibrationInProgress = false;
}