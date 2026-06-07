# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP32 tilt-compensated dive compass and dive computer. Reads heading from an HMC5883 magnetometer, corrects for tilt using an LSM303 accelerometer, reads depth and temperature from an MS5837-30BA pressure sensor, and renders a 4-zone diving computer UI on a 128×160 ST7735 TFT display. Includes a WiFi AP calibration portal for hard-iron / soft-iron magnetometer correction stored in NVS.

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
| 5 | SPI | MISO | microSD |
| 6 | SPI | CS | microSD |
| 9 | SPI | RST | ST7735 TFT |
| 10 | SPI | DC | ST7735 TFT |
| 11 | SPI | CS | ST7735 TFT |
| 1 | I2C (Wire) | SDA | HMC5883 + LSM303 |
| 2 | I2C (Wire) | SCL | HMC5883 + LSM303 |
| 8 | I2C (Wire1) | SDA | MS5837-30BA |
| 13 | I2C (Wire1) | SCL | MS5837-30BA |
| 48 | — | NeoPixel | LED RGB integrado |

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

## Source Files

| File | Purpose |
|------|---------|
| [`src/main.cpp`](src/main.cpp) | Main firmware: sensors, TFT, WiFi, SD, FreeRTOS tasks |
| [`src/deco.h`](src/deco.h) | Bühlmann ZHL-16C decompression engine (self-contained, no dynamic alloc) |

## Dual-Core Architecture (ESP32-S3)

The firmware uses both Xtensa LX7 cores via FreeRTOS:

| Core | Task | Responsibility | Rate |
|------|------|----------------|------|
| Core 1 | `loop()` (Arduino default) | Compass, accelerometer, TFT render, WiFi, DNS, SD | ~33 Hz |
| Core 0 | `depthTask` | Depth sensor + Bühlmann ZHL-16C deco calculations | 10 Hz |

Shared state (`g_depth`, `g_tempC`, `g_deco`) is protected by `g_dataMutex` (FreeRTOS mutex). Core 1 uses a 5 ms timeout when taking the mutex to avoid blocking the render loop.

**I2C bus separation:** `Wire` (GPIO 1/2) is used exclusively from Core 1 for compass + accelerometer. `Wire1` (GPIO 8/13) is used exclusively from Core 0 for the MS5837 pressure sensor. Never cross these buses between cores.

**Initialization order** (`setup()`):
1. Serial (USB CDC — waits up to 2 s for enumeration)
2. TFT via SPI (`SPI.begin(TFT_SCLK, -1, TFT_MOSI)` → `tft.initR(INITR_BLACKTAB)` → `setRotation(1)`)
3. I2C via `Wire.begin(I2C_SDA, I2C_SCL)` (pins from build flags)
3b. MS5837 via `Wire1.begin(8, 13)` — if not detected, `depthTask` falls back to `diveProfile()` simulation
4. HMC5883 magnetometer
5. LSM303 accelerometer (`address 0x18`, SA0=GND)
6. Load calibration from NVS (`Preferences` namespace `"compass"`)
7. WiFi AP + HTTP server (`setupWiFi()`)
8. Create `g_dataMutex` + launch `depthTask` on Core 0 (priority 1, 4 KB stack)

**Core 1 data flow (every 30 ms in `loop()`):**
1. `dnsServer.processNextRequest()` + `server.handleClient()`
2. `accel.getEvent()` + `mag.getEvent()` → raw sensor data
3. If calibrating: update min/max accumulators for hard-iron calculation
4. `calculateHeading(magEv, accEv)` → tilt-compensated heading (0–360°), stored in `g_heading`
5. Read `g_depth` + `g_deco` from shared globals (mutex-protected, 5 ms timeout)
6. Selective redraw: `drawHeadingStrip()`, `drawDepth()`, `drawDiveData()`, `drawCompassBar()`
7. `logToSD()` every 5 s

**Core 0 data flow (every 100 ms in `depthTask`):**
1. If `s_ms5837Ok`: `pressureSensor.read()` → `depth = pressureSensor.depth()`, `tempC = pressureSensor.temperature()`; else: `diveProfile()` simulation
2. `engine.update(depth, 0.1f)` — integrate Haldane equation
3. `engine.calculate(depth)` → `DecoResult {ndl_min, ceiling_m, in_deco}`
4. Write `g_depth` + `g_tempC` + `g_deco` under mutex

**`calculateHeading()` algorithm:**
1. Remap physical sensor axes to formula frame: sensor-X=down, sensor-Y=left, sensor-Z=forward → formula X=forward, Y=left, Z=up
2. Apply hard-iron offsets + soft-iron scales from `cal` struct
3. Compute pitch and roll from accelerometer
4. Tilt-compensate magnetometer → horizontal components Xh, Yh
5. `atan2(Yh, Xh)` + magnetic declination (−0.011 rad, tuned for Alcalá la Real)
6. Normalize to 0–360°

## Bühlmann ZHL-16C Decompression Engine (`src/deco.h`)

Self-contained implementation. No `new`/`malloc`, no STL, no stdlib beyond `<math.h>`. Safe for FreeRTOS stack allocation.

**Algorithm summary:**
- 16 N₂ tissue compartments with individual half-times (5–635 min) and a/b coefficients
- Haldane gas loading: `P_t += (P_alv - P_t) × (1 - e^(-ln2/t½ × Δt))`
- Alveolar N₂: `P_alv = (P_amb - 0.0627) × 0.7902`  (water vapour + air fraction)
- M-value with GF: `M_surf = 1 + GF × (a + 1/b - 1)`
- NDL (analytical): `t = -ln(1 - (M_surf - P_t)/(P_alv - P_t)) / k`
- Ceiling: `P_ceil = (P_t - GF×a) / (1 + GF×(1/b - 1))`

**Configuration constants in `deco.h`:**

| Constant | Value | Description |
|----------|-------|-------------|
| `DECO_GF` | 0.85 | Gradient factor (recreational moderate) |
| `DECO_FN2` | 0.7902 | N₂ fraction in air |
| `DECO_PH2O` | 0.0627 bar | Water vapour pressure at 37 °C |
| `DECO_N` | 16 | Number of compartments |

**API:**
```cpp
DecoEngine engine;
engine.init();                          // tissues → surface saturation
engine.update(depth_m, dt_s);          // integrate Haldane for dt_s seconds
DecoResult r = engine.calculate(depth_m);
// r.ndl_min    — minutes of NDL remaining (0 if in deco)
// r.ceiling_m  — ascent ceiling in metres (0 if no deco)
// r.in_deco    — true if decompression required
```

## Screen Layout (rotation=1 → 160×128 landscape)

```
y   0..15   Heading strip: degrees (cyan) + cardinal label (white, textSize=2)
y  16..77   Depth zone: "PROF" label + large green number (textSize=4) + "m" unit
y  78..95   Data zone: NDL in minutes (green/yellow) or "DECO" (red) + temp/ceiling
y  96..127  Compass bar (32 px): scrolling tick marks + cardinal labels + fixed red triangle pointer
```

Data zone display logic:
- `NDL XX'` green — NDL > 5 min, no decompression
- `NDL  X'` yellow — NDL ≤ 5 min, approaching limit
- `DECO   ` red + `CEI X.Xm` red — decompression required

Divider lines drawn once by `drawFrame()` at y=16, y=78, y=96.

**Rendering functions:**
- `drawHeadingStrip(heading)` — skips if heading int unchanged
- `drawDepth(depth)` — skips if depth×10 int unchanged (≥0.1 m resolution)
- `drawDiveData(tempC, deco)` — skips NDL if same minute, skips temp/ceiling if same 0.1 unit
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
| `/status` | JSON: state, samples, min/max, offset, scale, heading, depth, ndl, ceiling, in_deco |
| `/data` | Serve dive log CSV from SD card |
| `/clearlog` | Clear dive log on SD card |

`Calibration` struct: `ox/oy/oz` (hard-iron offsets in µT), `sx/sy/sz` (soft-iron scales). Persisted in `Preferences` namespace `"compass"`.

## Key Config Details

- USB CDC: `ARDUINO_USB_MODE=1`, `ARDUINO_USB_CDC_ON_BOOT=1` — Serial waits up to 2 s (won't block indefinitely when running on battery).
- SPI must be initialized explicitly with `SPI.begin(TFT_SCLK, -1, TFT_MOSI)` before `tft.initR()`.
- LSM303 I2C address is `0x18` (SA0 pin tied to GND); default library address is `0x19`.
- MS5837-30BA address: `0x76` (CSB to GND). Power: 3.3 V only (1.5–3.6 V range). `setFluidDensity(1025)` for sea water, `1000` for fresh water.
- If MS5837 is not detected at boot, `depthTask` falls back to `diveProfile()` simulation and `g_tempC` stays at 24.5 °C — check serial output for "MS5837 no detectado".
- `depthTask` stack is 4 KB; increase to 8 KB when adding full deco stop schedule calculation.
- `src/main.xxcpp` is an older prototype (magnetometer only, no TFT, no tilt compensation) kept for reference — it is not compiled.
