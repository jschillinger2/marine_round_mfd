# Marine Round MFD (Multi-Function Display)

⚠️ **WARNING: This is an EXPERIMENTAL and UNTESTED version. Proceed with caution. Hardware has not been physically validated with this exact firmware compilation yet.**

This project implements a smartwatch-style round Multi-Function Display (MFD) for marine vessels using the **Waveshare ESP32-S3-Touch-LCD-1.46C** development board. It runs on top of **SensESP v3**, **LVGL v8.4**, and **Espressif Display Panel v1.x** under the **Arduino ESP32 Core v3.0.x / ESP-IDF v5.3** toolchain.

---

## UI Architecture & Layout

The UI utilizes a swipeable horizontal layout (`lv_tileview`) with four pages tailored for smartwatch-style round screen interactions (412x412 pixels, centered within the circular safe zone):

1. **Navigation Page (Tile 0,0):** 
   - Speed Over Ground (SOG) arc speedometer gauge (0–30 KT) with digital center reading.
   - Large magnetic/true heading digital text (e.g., `HDG: 245°`).
2. **Wind Page (Tile 1,0):** 
   - 360-degree Apparent Wind Angle (AWA) dial with a red indicator needle.
   - Centered digital Apparent Wind Speed (AWS) display.
3. **Propulsion Page (Tile 2,0):**
   - Circular Engine RPM Tachometer gauge (0–5000 RPM) with an orange needle.
   - Dual horizontal bar gauges for **Engine Temperature** and **Alternator Temperature** (0–120°C).
4. **System Status Page (Tile 3,0):**
   - Large digital Clock (local time synced).
   - Real-time WiFi Connection status display.
   - Real-time Signal K websocket subscription connection state.

---

## Signal K Subscriptions

The display acts as a Signal K client, automatically subscribing to the following standard paths:
* `navigation.speedOverGround` (converted from m/s to Knots)
* `navigation.headingMagnetic` (converted from Radians to Degrees)
* `environment.wind.angleApparent` (converted from Radians to Degrees)
* `environment.wind.speedApparent` (converted from m/s to Knots)
* `propulsion.engine.revolutions` (converted from Hz to RPM)
* `propulsion.engine.temperature` (converted from Kelvin to Celsius)
* `electrical.alternators.alternator.temperature` (converted from Kelvin to Celsius)

---

## Build Prerequisites

To compile this project, PlatformIO needs to be installed. Since it relies on the cutting edge **Arduino Core 3.x**, you should use the community-supported `pioarduino` platform fork which is natively configured in `platformio.ini`.

### Virtual Environment Setup (Optional/Recommended)
If you do not have PlatformIO globally, you can set it up inside a local Python virtual environment:
```bash
# Create python virtualenv
python3 -m venv ~/.platformio-env

# Install platformio inside virtualenv
~/.platformio-env/bin/pip install platformio
```

---

## Compilation

Run the compilation using your PlatformIO core executable:

```bash
# Compile using virtual environment PlatformIO Core
~/.platformio-env/bin/pio run

# Alternatively, if platformio is installed globally
pio run
```

Upon a successful build, PlatformIO generates:
* **`firmware.bin`** (Application binary)
* **`firmware.factory.bin`** (Combined binary containing bootloader, partition table, and application, flashable directly at address `0x00`)

---

## Key Configuration Files

* `platformio.ini`: Declares dependencies, board targets (`esp32s3box` base), and C++17 compiler flags.
* `include/esp_panel_board_custom_conf.h`: Maps the specific hardware pins for the Waveshare 1.46" round display (SPD2010 QSPI bus pins and SPD2010 I2C Touch pins).
* `lib/esp_websocket_client/`: Local library containing the Espressif `esp_websocket_client` component source, required because WebSocket client headers are excluded in Arduino Core 3.x precompiled packages.
