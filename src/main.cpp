#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include "secrets.h"

// I2C pins (BME280)
constexpr uint8_t  SDA_PIN             = 21;
constexpr uint8_t  SCL_PIN             = 22;

// Fan wiring
// RELAY pins → relay module IN (HIGH = fan ON; flip logic below if your module is active-LOW)
// TACH pins  → fan tach wire (orange on most PC fans)
//              GPIO 34 & 35 are input-only on ESP32 — no internal pull-up.
//              Add a 10kΩ resistor from each tach pin to 3.3V on your breadboard.
constexpr uint8_t  RELAY_MAX_PIN       = 26;
constexpr uint8_t  RELAY_NORMAL_PIN    = 25;
constexpr uint8_t  TACH_MAX_PIN        = 34;
constexpr uint8_t  TACH_NORMAL_PIN     = 35;
constexpr uint8_t  TACH_PULSES_PER_REV = 2;   // standard for most PC fans

constexpr uint32_t SERIAL_BAUD         = 115200;
constexpr uint32_t READ_INTERVAL_MS    = 2000;
constexpr uint32_t PUSH_INTERVAL_MS    = 300000;
constexpr uint32_t RPM_INTERVAL_MS     = 2000;
constexpr float    SEA_LEVEL_HPA       = 1013.25f;

// Adafruit IO feed paths (MQTT topics)
#define AIO_SERVER          "io.adafruit.com"
#define AIO_PORT            1883
#define FEED_FAN_MAX_CMD    AIO_USERNAME "/feeds/fan-max-control"    // subscribe
#define FEED_FAN_NORMAL_CMD AIO_USERNAME "/feeds/fan-normal-control" // subscribe
#define FEED_FAN_MAX_RPM    AIO_USERNAME "/feeds/fan-max-rpm"        // publish
#define FEED_FAN_NORMAL_RPM AIO_USERNAME "/feeds/fan-normal-rpm"     // publish
#define FEED_TEMP           AIO_USERNAME "/feeds/temp"
#define FEED_HUMIDITY       AIO_USERNAME "/feeds/humidity"

// ── Globals ───────────────────────────────────────────────────────────────────

Adafruit_BME280 bme;
WebServer       server(80);
WiFiClient      mqttNetClient;
PubSubClient    mqtt(mqttNetClient);

bool          sensorReady  = false;
bool          fanMaxOn     = false;
bool          fanNormalOn  = false;
unsigned long lastReadMs   = 0;
unsigned long lastPushMs   = 0;
unsigned long lastRpmMs    = 0;

float temperatureC  = NAN;
float humidity      = NAN;
float pressureHpa   = NAN;
float altitudeM     = NAN;
float fanMaxRpm     = 0.0f;
float fanNormalRpm  = 0.0f;

volatile uint32_t tachMaxPulses    = 0;   // written by ISR
volatile uint32_t tachNormalPulses = 0;   // written by ISR

// ── Fan / RPM ─────────────────────────────────────────────────────────────────

void IRAM_ATTR onTachMaxPulse()    { tachMaxPulses++; }
void IRAM_ATTR onTachNormalPulse() { tachNormalPulses++; }

void setFanMax(bool on) {
  fanMaxOn = on;
  digitalWrite(RELAY_MAX_PIN, on ? HIGH : LOW);
  Serial.printf("Fan Max %s\n", on ? "ON" : "OFF");
}

void setFanNormal(bool on) {
  fanNormalOn = on;
  digitalWrite(RELAY_NORMAL_PIN, on ? HIGH : LOW);
  Serial.printf("Fan Normal %s\n", on ? "ON" : "OFF");
}

void updateRpm() {
  float seconds = RPM_INTERVAL_MS / 1000.0f;

  uint32_t maxPulses    = tachMaxPulses;    tachMaxPulses    = 0;
  uint32_t normalPulses = tachNormalPulses; tachNormalPulses = 0;

  fanMaxRpm    = (maxPulses    / (float)TACH_PULSES_PER_REV) / seconds * 60.0f;
  fanNormalRpm = (normalPulses / (float)TACH_PULSES_PER_REV) / seconds * 60.0f;
  Serial.printf("Fan Max RPM: %.0f  |  Fan Normal RPM: %.0f\n", fanMaxRpm, fanNormalRpm);
}

// ── Sensor ────────────────────────────────────────────────────────────────────

bool initSensor() {
  Wire.begin(SDA_PIN, SCL_PIN);
  if (bme.begin(0x76)) { Serial.println("BME280 at 0x76"); return true; }
  if (bme.begin(0x77)) { Serial.println("BME280 at 0x77"); return true; }
  return false;
}

void updateSensorValues() {
  if (!sensorReady) {
    sensorReady = initSensor();
    if (!sensorReady) { Serial.println("BME280 not found."); return; }
  }

  temperatureC = bme.readTemperature();
  humidity     = bme.readHumidity();
  pressureHpa  = bme.readPressure() / 100.0F;
  altitudeM    = bme.readAltitude(SEA_LEVEL_HPA);

  if (isnan(temperatureC) || isnan(humidity) || isnan(pressureHpa)) {
    Serial.println("Sensor read failed.");
    sensorReady = false;
    return;
  }

  float tempF = temperatureC * 9.0f / 5.0f + 32.0f;
  Serial.printf("Temp: %.1f°C (%.1f°F)  Hum: %.1f%%  Pres: %.1f hPa\n",
                temperatureC, tempF, humidity, pressureHpa);
}

// ── Adafruit IO REST (push sensor data) ──────────────────────────────────────

void postFeed(const char* feedKey, float value) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String url = String("https://io.adafruit.com/api/v2/")
               + AIO_USERNAME + "/feeds/" + feedKey + "/data";
  http.begin(client, url);
  http.addHeader("X-AIO-Key", AIO_KEY);
  http.addHeader("Content-Type", "application/json");
  String body = "{\"value\":\"" + String(value, 1) + "\"}";
  int code = http.POST(body);
  Serial.printf("AIO REST [%s] → %d\n", feedKey, code);
  http.end();
}

void pushToAdafruitIO() {
  if (WiFi.status() != WL_CONNECTED || !sensorReady) return;
  if (isnan(temperatureC) || isnan(humidity)) return;

  float tempF = temperatureC * 9.0f / 5.0f + 32.0f;
  postFeed("temp",     tempF);
  postFeed("humidity", humidity);
}

// ── Adafruit IO MQTT (receive fan commands) ───────────────────────────────────

void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  msg.trim();
  Serial.printf("MQTT [%s] → \"%s\"\n", topic, msg.c_str());

  bool turnOn = (msg == "1" || msg.equalsIgnoreCase("ON"));
  if (String(topic) == FEED_FAN_MAX_CMD)    setFanMax(turnOn);
  if (String(topic) == FEED_FAN_NORMAL_CMD) setFanNormal(turnOn);
}

void ensureMqttConnected() {
  if (mqtt.connected()) return;
  if (WiFi.status() != WL_CONNECTED) return;

  Serial.print("MQTT connecting...");
  // client ID must be unique per device
  String clientId = "esp32-printer-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  if (mqtt.connect(clientId.c_str(), AIO_USERNAME, AIO_KEY)) {
    Serial.println(" connected.");
    mqtt.subscribe(FEED_FAN_MAX_CMD);
    mqtt.subscribe(FEED_FAN_NORMAL_CMD);
    Serial.println("Subscribed to fan-max-control and fan-normal-control feeds.");
  } else {
    Serial.printf(" failed, rc=%d\n", mqtt.state());
  }
}

// ── Web dashboard ─────────────────────────────────────────────────────────────

String buildDashboardHtml() {
  float tempF = isnan(temperatureC) ? NAN : temperatureC * 9.0f / 5.0f + 32.0f;

  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>3D Printer Monitor</title>";
  html += "<style>"
          "body{font-family:Arial,sans-serif;margin:0;padding:16px;background:#f4f7fb;color:#222;}"
          "h1{margin-top:0;font-size:1.3rem;}"
          ".card{background:#fff;border-radius:10px;box-shadow:0 1px 6px rgba(0,0,0,.12);padding:16px;margin-bottom:16px;}"
          "table{width:100%;border-collapse:collapse;}"
          "td{padding:10px 8px;border-bottom:1px solid #eee;font-size:1.1rem;}"
          "td:last-child{text-align:right;font-weight:bold;}"
          ".btn{display:inline-block;padding:10px 24px;border-radius:8px;border:none;font-size:1rem;cursor:pointer;text-decoration:none;color:#fff;margin:4px;}"
          ".btn-on{background:#2ea44f;} .btn-off{background:#cf222e;}"
          "</style>";
  html += "<meta http-equiv='refresh' content='10'></head><body>";
  html += "<h1>3D Printer Monitor</h1>";

  html += "<div class='card'><strong>Wi-Fi:</strong> ";
  html += WiFi.isConnected() ? "Connected" : "Disconnected";
  html += " &nbsp;|&nbsp; <strong>IP:</strong> " + WiFi.localIP().toString() + "</div>";

  // Fan Max card
  html += "<div class='card'>";
  html += "<strong>Fan Max:</strong> " + String(fanMaxOn ? "ON" : "OFF");
  html += " &nbsp;|&nbsp; <strong>RPM:</strong> " + String((int)fanMaxRpm);
  html += "<br><br>";
  html += "<a class='btn btn-on'  href='/fan?which=max&state=on'>Max ON</a>";
  html += "<a class='btn btn-off' href='/fan?which=max&state=off'>Max OFF</a>";
  html += "</div>";

  // Fan Normal card
  html += "<div class='card'>";
  html += "<strong>Fan Normal:</strong> " + String(fanNormalOn ? "ON" : "OFF");
  html += " &nbsp;|&nbsp; <strong>RPM:</strong> " + String((int)fanNormalRpm);
  html += "<br><br>";
  html += "<a class='btn btn-on'  href='/fan?which=normal&state=on'>Normal ON</a>";
  html += "<a class='btn btn-off' href='/fan?which=normal&state=off'>Normal OFF</a>";
  html += "</div>";

  // Sensor card
  if (!sensorReady) {
    html += "<div class='card'>Sensor not ready — check wiring.</div>";
  } else {
    html += "<div class='card'><table>";
    if (!isnan(tempF))
      html += "<tr><td>Temperature</td><td>" + String(tempF, 1) + " &deg;F (" + String(temperatureC, 1) + " &deg;C)</td></tr>";
    if (!isnan(humidity))
      html += "<tr><td>Humidity</td><td>" + String(humidity, 1) + " %</td></tr>";
    if (!isnan(pressureHpa))
      html += "<tr><td>Pressure</td><td>" + String(pressureHpa, 1) + " hPa</td></tr>";
    if (!isnan(altitudeM))
      html += "<tr><td>Altitude</td><td>" + String(altitudeM, 1) + " m</td></tr>";
    html += "</table></div>";
  }

  html += "<div class='card'><small>Refreshes every 10 s &nbsp;|&nbsp; JSON: <code>/json</code></small></div>";
  html += "</body></html>";
  return html;
}

String buildJson() {
  float tempF = isnan(temperatureC) ? NAN : temperatureC * 9.0f / 5.0f + 32.0f;
  String j = "{";
  j += "\"sensorReady\":"  + String(sensorReady ? "true" : "false") + ",";
  j += "\"temperatureC\":" + String(temperatureC, 1) + ",";
  j += "\"temperatureF\":" + String(tempF, 1) + ",";
  j += "\"humidity\":"     + String(humidity, 1) + ",";
  j += "\"pressureHpa\":"  + String(pressureHpa, 1) + ",";
  j += "\"altitudeM\":"    + String(altitudeM, 1) + ",";
  j += "\"fanMaxOn\":"      + String(fanMaxOn    ? "true" : "false") + ",";
  j += "\"fanMaxRpm\":"    + String((int)fanMaxRpm) + ",";
  j += "\"fanNormalOn\":"  + String(fanNormalOn ? "true" : "false") + ",";
  j += "\"fanNormalRpm\":" + String((int)fanNormalRpm);
  j += "}";
  return j;
}

void handleRoot()    { server.send(200, "text/html",        buildDashboardHtml()); }
void handleJson()    { server.send(200, "application/json", buildJson()); }
void handleNotFound(){ server.send(404, "text/plain",       "Not found"); }

void handleFan() {
  String which = server.arg("which");
  String state = server.arg("state");
  bool on = (state == "on");
  if (which == "max")    setFanMax(on);
  if (which == "normal") setFanNormal(on);
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "");
}

// ── Setup / Loop ──────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(1000);
  Serial.println("\nESP32 3D Printer Monitor starting...");

  // Relay outputs — both fans start OFF
  pinMode(RELAY_MAX_PIN,    OUTPUT);
  pinMode(RELAY_NORMAL_PIN, OUTPUT);
  setFanMax(false);
  setFanNormal(false);

  // Tach inputs — GPIO 34/35 have no internal pull-up, use external 10kΩ to 3.3V
  pinMode(TACH_MAX_PIN,    INPUT);
  pinMode(TACH_NORMAL_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(TACH_MAX_PIN),    onTachMaxPulse,    RISING);
  attachInterrupt(digitalPinToInterrupt(TACH_NORMAL_PIN), onTachNormalPulse, RISING);

  sensorReady = initSensor();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("Connecting to '%s'...\n", WIFI_SSID);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 30000) {
    delay(500);
    Serial.printf("  status=%d\n", WiFi.status());
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("Wi-Fi connected! IP: %s  RSSI: %d dBm\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
    updateSensorValues();
    pushToAdafruitIO();          // first reading on boot, don't wait 5 min
    lastPushMs = millis();
  } else {
    Serial.println("Wi-Fi FAILED — dashboard unavailable.");
  }

  mqtt.setServer(AIO_SERVER, AIO_PORT);
  mqtt.setCallback(onMqttMessage);

  server.on("/",         handleRoot);
  server.on("/json",     handleJson);
  server.on("/fan",      handleFan);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("HTTP server started.");
}

void loop() {
  server.handleClient();

  // Keep MQTT alive and receiving messages
  ensureMqttConnected();
  mqtt.loop();

  unsigned long now = millis();

  if (now - lastReadMs >= READ_INTERVAL_MS) {
    lastReadMs = now;
    updateSensorValues();
  }

  if (now - lastRpmMs >= RPM_INTERVAL_MS) {
    lastRpmMs = now;
    updateRpm();
  }

  if (now - lastPushMs >= PUSH_INTERVAL_MS) {
    lastPushMs = now;
    pushToAdafruitIO();
  }
}
