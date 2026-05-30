# Diagrama de Conexiones — Brujula (Compass)

## 1. Conexiones de Hardware (Pines ESP32-C3)

```mermaid
graph TD
    ESP32["🟦 ESP32-C3 DevKitC-02"]

    subgraph SPI_BUS["Bus SPI — 27 MHz"]
        TFT["📺 TFT ST7735\n128×160 px"]
    end

    subgraph I2C_BUS["Bus I2C — Wire"]
        MAG["🧲 HMC5883\nMagnetómetro"]
        ACC["📐 LSM303\nAcelerómetro"]
    end

    USB["💻 USB CDC\n115200 baud"]

    ESP32 -->|"GPIO 9 — SCLK"| SPI_BUS
    ESP32 -->|"GPIO 8 — MOSI"| SPI_BUS
    ESP32 -->|"GPIO 2 — CS"| TFT
    ESP32 -->|"GPIO 1 — DC"| TFT
    ESP32 -->|"GPIO 0 — RST"| TFT

    ESP32 -->|"GPIO 6 — SDA"| I2C_BUS
    ESP32 -->|"GPIO 7 — SCL"| I2C_BUS

    ESP32 <-->|"USB nativo"| USB
```

---

## 2. Tabla de Pines

| GPIO | Función | Periférico |
|------|---------|-----------|
| 0 | RST | TFT ST7735 |
| 1 | DC | TFT ST7735 |
| 2 | CS | TFT ST7735 |
| 6 | SDA | I2C → HMC5883 + LSM303 |
| 7 | SCL | I2C → HMC5883 + LSM303 |
| 8 | MOSI | SPI → TFT ST7735 |
| 9 | SCLK | SPI → TFT ST7735 |
| USB | CDC | Monitor Serie 115200 |

---

## 3. Dependencias de Software

```mermaid
graph TD
    MAIN["main.cpp\n(lógica principal)"]

    subgraph DISPLAY["Capa de Pantalla"]
        ST7735["Adafruit_ST7735"]
        GFX["Adafruit_GFX"]
        SPI_LIB["SPI.h"]
    end

    subgraph SENSORS["Capa de Sensores"]
        HMC["Adafruit_HMC5883_U"]
        LSM["Adafruit_LSM303_Accel"]
        SENS_BASE["Adafruit_Sensor.h\n(abstracción base)"]
        WIRE["Wire.h (I2C)"]
    end

    MAIN --> ST7735
    MAIN --> HMC
    MAIN --> LSM
    MAIN --> SPI_LIB
    MAIN --> WIRE

    ST7735 --> GFX
    ST7735 --> SPI_LIB
    HMC --> SENS_BASE
    LSM --> SENS_BASE
```

---

## 4. Flujo de Datos (Loop Principal — cada 30 ms)

```mermaid
flowchart LR
    subgraph HARDWARE["Hardware"]
        MAG_HW["HMC5883\nMagnetómetro\nX, Y, Z"]
        ACC_HW["LSM303\nAcelerómetro\nX, Y, Z"]
    end

    subgraph PROCESO["Procesamiento (main.cpp)"]
        READ["getEvent()\nLeer sensores"]
        CALC["calculateHeading()\n1. Calcular pitch y roll\n2. Compensación de inclinación\n3. Corrección declinación\n   (−0.011 rad ≈ Alcalá la Real)\n4. Normalizar 0–360°"]
        DRAW["drawGameCompass()\nDibujar brújula\nFOV 90° centrado\nen rumbo actual"]
    end

    subgraph SALIDAS["Salidas"]
        TFT_OUT["TFT ST7735\nBrújula visual\n+ rumbo en grados"]
        SERIAL_OUT["Monitor Serie\n115200 baud\nrumbo numérico"]
    end

    MAG_HW -->|"Campo magnético"| READ
    ACC_HW -->|"Aceleración"| READ
    READ -->|"sensor_event_t"| CALC
    CALC -->|"headingDegrees\n(float 0–360°)"| DRAW
    DRAW --> TFT_OUT
    CALC --> SERIAL_OUT
```

---

## 5. Algoritmo de Cálculo de Rumbo

```mermaid
flowchart TD
    A["Leer magnetómetro\nmag.X, mag.Y, mag.Z"] --> C
    B["Leer acelerómetro\nacc.X, acc.Y, acc.Z"] --> C

    C["Calcular orientación\npitch = atan2(accX, √(accY²+accZ²))\nroll  = atan2(accY, accZ)"]

    C --> D["Compensación de inclinación\nXh = magX·cos(pitch) + magZ·sin(pitch)\nYh = magX·sin(roll)·sin(pitch) + magY·cos(roll)\n    − magZ·sin(roll)·cos(pitch)"]

    D --> E["heading = atan2(Yh, Xh)"]

    E --> F["Aplicar declinación magnética\nheading += −0.011 rad"]

    F --> G["Normalizar a 0–360°"]

    G --> H["Rumbo final en grados"]
```

---

## 6. Vista General del Sistema

```
                    ┌─────────────────────────────────────────────────────┐
                    │              ESP32-C3 DevKitC-02                    │
                    │                                                     │
  ┌──────────┐      │  GPIO 9 ──── SCLK ──┐                             │
  │ TFT      │      │  GPIO 8 ──── MOSI ──┤  SPI @ 27 MHz              │
  │ ST7735   │◄─────│  GPIO 2 ──── CS ────┘                             │
  │ 128×160  │      │  GPIO 1 ──── DC                                   │
  └──────────┘      │  GPIO 0 ──── RST                                  │
                    │                                                     │
  ┌──────────┐      │  GPIO 6 ──── SDA ───┐                             │
  │ HMC5883  │◄────►│  GPIO 7 ──── SCL ───┤  I2C                       │
  │ (mag)    │      │                     │                             │
  └──────────┘      │                     │                             │
                    │                                                     │
  ┌──────────┐      │                                                     │
  │ LSM303   │◄────►│  (mismo bus I2C)                                  │
  │ (accel)  │      │                                                     │
  └──────────┘      │                                                     │
                    │                                                     │
  ┌──────────┐      │  USB CDC ──── Monitor Serie @ 115200              │
  │ PC/Debug │◄─────│                                                   │
  └──────────┘      │                                                     │
                    └─────────────────────────────────────────────────────┘
```
