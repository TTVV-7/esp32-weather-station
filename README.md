# ESP32 + Waveshare Environmental Sensor

This project reads **temperature, humidity, and pressure** from a Waveshare environmental sensor that uses the **BME280** chip.

## Wiring for your 6-wire sensor

Your sensor labels (`CS`, `MISO`, `SCL`, `SDA`, `GND`, `VCC`) match a **BME280-style dual-interface board**.
For this project, use the sensor in **I2C mode**:

| Sensor wire | ESP32 pin |
| --- | --- |
| `VCC` | `3V3` |
| `GND` | `GND` |
| `SCL` | `GPIO22` |
| `SDA` | `GPIO21` |
| `CS` | `3V3` |
| `MISO` | `GND` for address `0x76`, or `3V3` for `0x77` |

> If your board uses different I2C pins, change `SDA_PIN` and `SCL_PIN` in `src/main.cpp`.

### Why this works

On these boards, `SCL`/`SDA` are used for **I2C**, while `CS` and `MISO` are used to select the mode/address.
You do **not** need full SPI wiring for the current sketch.

## What it does

- Starts the BME280 sensor on I2C address `0x76` or `0x77`
- Prints readings to the serial monitor every 2 seconds
- Retries if the sensor is not found

## Upload in VS Code with PlatformIO

1. Install the **PlatformIO IDE** extension in VS Code.
2. Open this folder.
3. Connect your ESP32 with USB.
4. Build: `PlatformIO: Build`
5. Upload: `PlatformIO: Upload`
6. Open serial monitor at `115200` baud.

## Wi-Fi dashboard

1. Edit `src/main.cpp` and set `WIFI_SSID` and `WIFI_PASSWORD` to your network credentials.
2. Upload the sketch again.
3. Open the serial monitor and wait for the ESP32 to connect to Wi-Fi.
4. Use the IP address printed to the serial monitor in your browser, for example:
   - `http://192.168.1.42`
5. The dashboard refreshes automatically every 5 seconds.

### What the dashboard gives you

- Temperature, humidity, pressure, and altitude readings
- Local access from any device on your Wi-Fi network
- A JSON endpoint at `/json` for integration with other tools

## Remote access when you are away from home

This code only serves the dashboard on your local home network.
To reach it from outside your house, you need one of these:

- **Port forwarding** on your router + dynamic DNS
- **VPN to your home network**
- **Cloud tunnel services** such as Cloudflare Tunnel or ngrok

> Direct Alexa control is not available from this local sketch alone. Alexa integration typically requires a cloud service, a smart home skill, or a third-party bridge.

## If your sensor is not detected

- Make sure the sensor is a **BME280** (not BMP280, which has no humidity).
- Check whether the I2C address is `0x76` or `0x77`.
- Confirm the ESP32 is powering the sensor with `3.3V`.
