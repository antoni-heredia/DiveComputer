# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP32-C3 tilt-compensated digital compass. Reads heading from an HMC5883 magnetometer, corrects for tilt using an LSM303 accelerometer, and renders a scrolling game-style compass rose on a 128×160 ST7735 TFT display.

## PlatformIO Commands

```bash
pio run                          # Build
pio run --target upload          # Flash to ESP32-C3
pio device monitor               # Open serial monitor (115200 baud)
pio run --target upload && pio device monitor  # Flash + monitor
pio run --target clean           # Clean build artifacts
```

## Architecture

Everything lives in [`src/main.cpp`](src/main.cpp). There are no separate modules — setup, sensor logic, and rendering are all in one file.

**Initialization order matters** (`setup()`):
1. Serial (USB CDC — wait for connection)
2. TFT via SPI (`SPI.begin(9, -1, 8)` then `tft.initR(INITR_BLACKTAB)`)
3. I2C via `Wire.begin(6, 7)`
4. HMC5883 magnetometer
5. LSM303 accelerometer

**Data flow (every 30 ms in `loop()`):**
- `accel.getEvent()` + `mag.getEvent()` → raw sensor data
- `calculateHeading(magEvent, accEvent)` → tilt-compensated heading in degrees (0–360°)
- `drawGameCompass(heading)` → updates TFT only if heading changed by ≥ 1°

**`calculateHeading()` algorithm:**
1. Compute pitch and roll from accelerometer axes
2. Apply tilt compensation to magnetometer X/Y/Z → horizontal components Xh, Yh
3. `atan2(Yh, Xh)` + magnetic declination (−0.011 rad, tuned for Alcalá la Real)
4. Normalize to 0–360°

**`drawGameCompass()` rendering:**
- 90° FOV centered on current heading; marks every 15° across the 128 px width
- Cardinal labels (N/E/S/W) at 90° intervals, intermediate marks at 45°, minor ticks at 15°
- Selective redraw: only clears the compass strip (`fillRect`), not the full screen
- Static red pointer triangle drawn once in `setupTFT()` at center-top

## Pin Map

| GPIO | Bus | Signal | Peripheral |
|------|-----|--------|------------|
| 0 | SPI | RST | ST7735 TFT |
| 1 | SPI | DC | ST7735 TFT |
| 2 | SPI | CS | ST7735 TFT |
| 6 | I2C | SDA | HMC5883 + LSM303 |
| 7 | I2C | SCL | HMC5883 + LSM303 |
| 8 | SPI | MOSI | ST7735 TFT |
| 9 | SPI | SCLK | ST7735 TFT |

## Key Config Details

- USB CDC is enabled (`ARDUINO_USB_MODE=1`, `ARDUINO_USB_CDC_ON_BOOT=1`); `Serial.begin()` waits for USB enumeration before continuing.
- SPI must be initialized explicitly with `SPI.begin(9, -1, 8)` before calling `tft.initR()` — the ESP32-C3's default SPI pins differ from the ESP32 classic.
- `src/main.xxcpp` is an older prototype (magnetometer only, no TFT, no tilt compensation) kept for reference — it is not compiled.
