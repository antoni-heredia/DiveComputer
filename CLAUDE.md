# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP32 tilt-compensated dive compass. Reads heading from an HMC5883 magnetometer, corrects for tilt using an LSM303 accelerometer, and renders a 4-zone diving computer UI on a 128×160 ST7735 TFT display. Includes a WiFi AP calibration portal for hard-iron / soft-iron magnetometer correction stored in NVS.

## PlatformIO Commands

```bash
pio run                          # Build (default env)
pio run -e esp32-c3-devkitc-02   # Build for ESP32-C3
pio run -e esp32-s3-devkitc-1    # Build for ESP32-S3
pio run --target upload          # Flash to device
pio device monitor               # Open serial monitor (115200 baud)
pio run --target upload && pio device monitor  # Flash + monitor
pio run --target clean           # Clean build artifacts
```

## Target Boards

Two environments are defined in `platformio.ini`:

| env | Board | Active target |
|-----|-------|---------------|
| `esp32-c3-devkitc-02` | ESP32-C3-DevKitC-02 | — |
| `esp32-s3-devkitc-1` | ESP32-S3-DevKitC-1 | ✓ (pins match main.cpp #defines) |

The source file `src/main.cpp` has hardcoded `#define` pin values that match the S3 environment. The I2C pins (`I2C_SDA`, `I2C_SCL`) come from `platformio.ini` build flags.

## Pin Maps

### ESP32-S3 (`esp32-s3-devkitc-1`) — active

| GPIO | Bus | Signal | Peripheral |
|------|-----|--------|------------|
| 3 | SPI | MOSI | ST7735 TFT |
| 4 | SPI | SCLK | ST7735 TFT |
| 9 | SPI | RST | ST7735 TFT |
| 10 | SPI | DC | ST7735 TFT |
| 11 | SPI | CS | ST7735 TFT |
| 1 | I2C | SDA | HMC5883 + LSM303 |
| 2 | I2C | SCL | HMC5883 + LSM303 |

### ESP32-C3 (`esp32-c3-devkitc-02`)

| GPIO | Bus | Signal | Peripheral |
|------|-----|--------|------------|
| 8 | SPI | MOSI | ST7735 TFT |
| 9 | SPI | SCLK | ST7735 TFT |
| 0 | SPI | RST | ST7735 TFT |
| 1 | SPI | DC | ST7735 TFT |
| 2 | SPI | CS | ST7735 TFT |
| 6 | I2C | SDA | HMC5883 + LSM303 |
| 7 | I2C | SCL | HMC5883 + LSM303 |

## Architecture

Everything lives in [`src/main.cpp`](src/main.cpp). No separate modules — setup, sensor logic, calibration, WiFi server, and rendering are all in one file.

**Initialization order** (`setup()`):
1. Serial (USB CDC — waits up to 2 s for enumeration)
2. TFT via SPI (`SPI.begin(TFT_SCLK, -1, TFT_MOSI)` → `tft.initR(INITR_BLACKTAB)` → `setRotation(1)`)
3. I2C via `Wire.begin(I2C_SDA, I2C_SCL)` (pins from build flags)
4. HMC5883 magnetometer
5. LSM303 accelerometer (`address 0x18`, SA0=GND)
6. Load calibration from NVS (`Preferences` namespace `"compass"`)
7. WiFi AP + HTTP server (`setupWiFi()`)

**Data flow (every 30 ms in `loop()`):**
1. `server.handleClient()` — serves calibration portal requests
2. `accel.getEvent()` + `mag.getEvent()` → raw sensor data
3. If calibrating: update min/max accumulators for hard-iron calculation
4. `calculateHeading(magEv, accEv)` → tilt-compensated heading (0–360°), stored in `g_heading`
5. Simulate depth (`g_depth`) and temperature (`g_tempC`) — **placeholder, no real pressure sensor yet**
6. Selective redraw: `drawHeadingStrip()`, `drawDepth()`, `drawDiveData()`, `drawCompassBar()` (each only updates when its value changes)

**`calculateHeading()` algorithm:**
1. Remap physical sensor axes to formula frame: sensor-X=down, sensor-Y=left, sensor-Z=forward → formula X=forward, Y=left, Z=up
2. Apply hard-iron offsets + soft-iron scales from `cal` struct
3. Compute pitch and roll from accelerometer
4. Tilt-compensate magnetometer → horizontal components Xh, Yh
5. `atan2(Yh, Xh)` + magnetic declination (−0.011 rad, tuned for Alcalá la Real)
6. Normalize to 0–360°

## Screen Layout (rotation=1 → 160×128 landscape)

```
y   0..15   Heading strip: degrees (cyan) + cardinal label (white, textSize=2)
y  16..77   Depth zone: "PROF" label + large green number (textSize=4) + "m" unit
y  78..95   Data zone: dive time (yellow, left) + temperature (orange, right)
y  96..127  Compass bar (32 px): scrolling tick marks + cardinal labels + fixed red triangle pointer
```

Divider lines drawn once by `drawFrame()` at y=16, y=78, y=96.

**Rendering functions:**
- `drawHeadingStrip(heading)` — skips if heading int unchanged
- `drawDepth(depth)` — skips if depth×10 int unchanged (≥0.1 m resolution)
- `drawDiveData(tempC, elapsedMs)` — skips time if same second, skips temp if same 0.1°C
- `drawCompassBar(heading)` — always redraws when called; 90° FOV, marks every 15°, cardinals every 90°, intermediates every 45°
- `showSplash()` — dive flag + "Toni Heredia", shown once during `setupTFT()`

## Calibration System

WiFi AP SSID `"Brujula-Calib"` (open network). Connect and open `http://192.168.4.1`.

| Endpoint | Action |
|----------|--------|
| `/` | Calibration web portal (served from `PAGE_HTML` in PROGMEM) |
| `/start` | Begin collecting min/max samples |
| `/stop` | Compute hard-iron offsets + soft-iron scales, save to NVS |
| `/reset` | Reset offsets=0, scales=1, save to NVS |
| `/status` | JSON: state, samples, min/max, offset, scale, heading |

`Calibration` struct: `ox/oy/oz` (hard-iron offsets in µT), `sx/sy/sz` (soft-iron scales). Persisted in `Preferences` namespace `"compass"`.

## Key Config Details

- USB CDC: `ARDUINO_USB_MODE=1`, `ARDUINO_USB_CDC_ON_BOOT=1` — Serial waits up to 2 s (won't block indefinitely when running on battery).
- SPI must be initialized explicitly with `SPI.begin(TFT_SCLK, -1, TFT_MOSI)` before `tft.initR()`.
- LSM303 I2C address is `0x18` (SA0 pin tied to GND); default library address is `0x19`.
- `g_depth` and `g_tempC` are currently simulated (sine wave / placeholder) — replace with real pressure sensor readings.
- `src/main.xxcpp` is an older prototype (magnetometer only, no TFT, no tilt compensation) kept for reference — it is not compiled.
