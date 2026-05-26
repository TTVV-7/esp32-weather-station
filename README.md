# ESP32 Weather Station with Cloud-Controlled Ventilation

A self-contained IoT system built on an ESP32: it reads temperature, humidity, and pressure from a BME280 sensor, serves a live local dashboard over Wi-Fi, publishes telemetry to [Adafruit IO](https://io.adafruit.com) over MQTT, and lets you remotely switch two PC fans on/off with live RPM feedback measured by tachometer pulse counting.

Two firmware paths are included:

- **PlatformIO / Arduino** (`src/main.cpp`) — the primary implementation, with the full feature set above.
- **ESPHome** (`esphome/bme280_sensor.yaml`) — a declarative-config alternative for direct Home Assistant integration.

---

## Features

- **Environmental sensing.** Temperature, humidity, barometric pressure, and derived altitude from a BME280 over I²C (`0x76` or `0x77`).
- **Local web dashboard.** Auto-refreshing HTML page served from the ESP32; JSON endpoint at `/json` for downstream tooling.
- **Cloud telemetry & control via MQTT.** Publishes sensor data and fan RPM to Adafruit IO; subscribes to two command feeds for remote fan switching.
- **Closed-loop fan control.** Two relay-switched PC fans with tachometer feedback — RPM computed from pulse counts (`2 pulses/rev`) on input-only GPIOs.
- **Resilient connectivity.** Reconnects to Wi-Fi and MQTT broker on disconnect; retries sensor init.
- **Secrets hygiene.** Credentials kept in gitignored `secrets.h` / `secrets.yaml`, with `*.example.*` templates checked in.

## Architecture

```
        ┌─────────────────┐    I²C     ┌──────────────┐
        │ BME280 sensor   │───────────▶│              │
        └─────────────────┘            │              │           ┌─────────────────┐
                                       │              │   MQTT    │   Adafruit IO   │
        ┌─────────────────┐  GPIO/PWM  │    ESP32     │◀─────────▶│  (cloud broker) │
        │ 2× relays + fans│◀──────────│              │           └─────────────────┘
        │  tach signals   │──────────▶│              │
        └─────────────────┘            │              │     HTTP    ┌────────────┐
                                       │              │◀───────────▶│  Browser   │
                                       └──────────────┘             │ dashboard  │
                                                                    └────────────┘
```

## Hardware

| Component                  | Notes                                                                  |
| -------------------------- | ---------------------------------------------------------------------- |
| ESP32 dev board            | Any common ESP32 with USB                                              |
| Waveshare BME280 module    | 6-wire dual-interface board (I²C mode used here)                       |
| 2× relay modules           | Active-HIGH assumed; invert logic in code if yours is active-LOW       |
| 2× 4-pin PC fans           | Tach wire (typically orange) provides 2 pulses per revolution          |
| 2× 10 kΩ resistors         | Pull-ups from each tach pin to 3.3 V (GPIO 34/35 lack internal pulls)  |

### Wiring

**BME280 (I²C mode):**

| Sensor wire | ESP32 pin | Notes                                                |
| ----------- | --------- | ---------------------------------------------------- |
| `VCC`       | `3V3`     |                                                      |
| `GND`       | `GND`     |                                                      |
| `SCL`       | `GPIO22`  |                                                      |
| `SDA`       | `GPIO21`  |                                                      |
| `CS`        | `3V3`     | Selects I²C mode                                     |
| `MISO`      | `GND` or `3V3` | `GND` → address `0x76`, `3V3` → `0x77`         |

**Fans + relays:**

| Signal              | ESP32 pin |
| ------------------- | --------- |
| Relay 1 IN (max)    | `GPIO26`  |
| Relay 2 IN (normal) | `GPIO25`  |
| Tach 1 (max)        | `GPIO34`  |
| Tach 2 (normal)     | `GPIO35`  |

> GPIO 34 and 35 are **input-only** and have no internal pull-up. Add a 10 kΩ resistor from each tach pin to 3.3 V on the breadboard.

## Setup

### 1. Clone and configure secrets

```bash
git clone https://github.com/TTVV-7/esp32-weather-station.git
cd esp32-weather-station
cp src/secrets.example.h src/secrets.h
cp esphome/secrets.example.yaml esphome/secrets.yaml
```

Edit both files and fill in your Wi-Fi credentials and [Adafruit IO](https://io.adafruit.com) username + key.

### 2. Build & flash with PlatformIO

1. Install the **PlatformIO IDE** extension in VS Code.
2. Open this folder, connect the ESP32 over USB.
3. Run `PlatformIO: Build`, then `PlatformIO: Upload`.
4. Open the serial monitor at **115200 baud** — the ESP32 prints its local IP once Wi-Fi connects.

### 3. Use it

- **Local dashboard:** open the printed IP (e.g. `http://192.168.1.42`) in any browser on the same network. Auto-refreshes every 5 s. JSON at `/<ip>/json`.
- **Remote control:** in Adafruit IO, create feeds named `fan-max-control` and `fan-normal-control` (values `ON`/`OFF`). Live RPM is published back to `fan-max-rpm` and `fan-normal-rpm`. Sensor readings publish to `temp` and `humidity`.

### Alternative: ESPHome / Home Assistant

If you'd rather integrate directly with Home Assistant, flash `esphome/bme280_sensor.yaml` with the ESPHome CLI:

```bash
cd esphome
esphome run bme280_sensor.yaml
```

## Tech stack

`C++` · `Arduino framework` · `PlatformIO` · `ESP32` · `MQTT (PubSubClient)` · `Adafruit BME280` · `Adafruit IO` · `ESPHome` · `Home Assistant`

## Project layout

```
esp32-weather-station/
├── src/
│   ├── main.cpp              # Primary firmware (PlatformIO/Arduino)
│   └── secrets.example.h     # Template — copy to secrets.h
├── esphome/
│   ├── bme280_sensor.yaml    # ESPHome declarative config
│   └── secrets.example.yaml  # Template — copy to secrets.yaml
├── esp32_bme280/
│   └── esp32_bme280.ino      # Standalone Arduino sketch for sensor sanity check
├── platformio.ini            # PlatformIO build config
└── README.md
```

## Troubleshooting

- **Sensor not detected:** confirm it's a BME280 (not BMP280 — that one has no humidity). Try the alternate I²C address by moving `MISO` between `GND` and `3V3`.
- **Fan won't spin:** check whether your relay module is active-HIGH or active-LOW and flip the `digitalWrite` logic if needed.
- **RPM reads zero:** verify the 10 kΩ pull-up resistor on the tach pin; without it, the input-only GPIOs will float.
- **MQTT keeps disconnecting:** Adafruit IO's free tier has a connection rate limit; the reconnect loop in `main.cpp` already backs off, but check your Adafruit IO username/key are correct.

## License

MIT
