#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

// For 6-wire BME280-style boards labeled CS/MISO/SCL/SDA/GND/VCC,
// use I2C mode with: CS -> 3V3 and MISO -> GND (0x76) or 3V3 (0x77).
constexpr uint8_t SDA_PIN = 21;
constexpr uint8_t SCL_PIN = 22;
constexpr uint32_t SERIAL_BAUD = 115200;
constexpr uint32_t READ_INTERVAL_MS = 2000;
constexpr float SEA_LEVEL_HPA = 1013.25f;

Adafruit_BME280 bme;
bool sensorReady = false;
unsigned long lastReadMs = 0;

bool initSensor() {
  Wire.begin(SDA_PIN, SCL_PIN);

  if (bme.begin(0x76)) {
    Serial.println("BME280 detected at I2C address 0x76");
    return true;
  }

  if (bme.begin(0x77)) {
    Serial.println("BME280 detected at I2C address 0x77");
    return true;
  }

  return false;
}

void printReadings() {
  const float temperatureC = bme.readTemperature();
  const float humidity = bme.readHumidity();
  const float pressureHpa = bme.readPressure() / 100.0F;
  const float altitudeM = bme.readAltitude(SEA_LEVEL_HPA);

  if (isnan(temperatureC) || isnan(humidity) || isnan(pressureHpa)) {
    Serial.println("Failed to read from the BME280 sensor.");
    return;
  }

  Serial.println("-----------------------------");
  Serial.printf("Temperature: %.2f °C\n", temperatureC);
  Serial.printf("Humidity:    %.2f %%\n", humidity);
  Serial.printf("Pressure:    %.2f hPa\n", pressureHpa);
  Serial.printf("Altitude:    %.2f m\n", altitudeM);
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(1000);

  Serial.println();
  Serial.println("ESP32 + Waveshare BME280 sensor starting...");

  sensorReady = initSensor();

  if (!sensorReady) {
    Serial.println("BME280 not found. Check wiring, power, and the I2C address.");
  }
}

void loop() {
  if (!sensorReady) {
    delay(3000);
    sensorReady = initSensor();
    return;
  }

  if (millis() - lastReadMs >= READ_INTERVAL_MS) {
    lastReadMs = millis();
    printReadings();
  }
}
