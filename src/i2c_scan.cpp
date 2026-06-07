#include <Arduino.h>
#include <Wire.h>

// Pines I2C: se pueden sobreescribir con -D I2C_SDA=x -D I2C_SCL=y en platformio.ini
#ifndef I2C_SDA
  #define I2C_SDA 1   // default S3 Super Mini
#endif
#ifndef I2C_SCL
  #define I2C_SCL 2
#endif
#define SDA_PIN I2C_SDA
#define SCL_PIN I2C_SCL

static void readIdReg(uint8_t addr, uint8_t reg, const char* label) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return;
  Wire.requestFrom((int)addr, 1);
  if (Wire.available()) {
    uint8_t val = Wire.read();
    Serial.printf("    %s (reg 0x%02X): 0x%02X\n", label, reg, val);
  }
}

static void scanOnce() {
  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    uint8_t err = Wire.endTransmission();
    if (err == 0) {
      Serial.printf("  [OK] 0x%02X (%3d)", addr, addr);
      if      (addr == 0x1E) Serial.print("  <- HMC5883L / LSM303-Mag");
      else if (addr == 0x1D) Serial.print("  <- LSM303-Mag alt");
      else if (addr == 0x19) Serial.print("  <- LSM303-Accel (SA0=1, default)");
      else if (addr == 0x18) Serial.print("  <- LSM303-Accel (SA0=0)");
      else if (addr == 0x68) Serial.print("  <- MPU6050/MPU9250 (AD0=0)");
      else if (addr == 0x69) Serial.print("  <- MPU6050/MPU9250 (AD0=1)");
      else if (addr == 0x76) Serial.print("  <- MS5837 (CSB=GND) / BMP280");
      else if (addr == 0x77) Serial.print("  <- MS5837 (CSB=VCC) / BMP280");
      Serial.println();
      found++;
    } else if (err == 4) {
      Serial.printf("  [ERR4] 0x%02X - error de bus\n", addr);
    }
  }
  Serial.printf("  -> %d dispositivo(s) encontrado(s)\n", found);
}

void setup() {
  Serial.begin(115200);
  unsigned long t0 = millis();
  while (!Serial && (millis() - t0 < 3000)) { delay(10); }

  Serial.println("\n===============================");
  Serial.println("   ESCANER I2C - Brujula ESP32-C3");
  Serial.printf ("   SDA=GPIO%d  SCL=GPIO%d\n", SDA_PIN, SCL_PIN);
  Serial.println("===============================\n");

  Wire.begin(SDA_PIN, SCL_PIN);
  Serial.println("--- Primer escaneo ---");
  scanOnce();

  // ---- Intentar leer registros de identificacion ----
  Serial.println("\n--- Registros WHO_AM_I / ID ---");

  // HMC5883L: registros 0x0A-0x0C = 'H','4','3'
  Wire.beginTransmission(0x1E);
  Wire.write(0x0A);
  if (Wire.endTransmission(false) == 0) {
    Wire.requestFrom((int)0x1E, 3);
    if (Wire.available() >= 3) {
      uint8_t a = Wire.read(), b = Wire.read(), c = Wire.read();
      Serial.printf("  0x1E ID A/B/C: 0x%02X 0x%02X 0x%02X", a, b, c);
      if (a == 0x48 && b == 0x34 && c == 0x33)
        Serial.print("  -> HMC5883L CONFIRMADO");
      Serial.println();
    }
  } else {
    Serial.println("  0x1E no responde");
  }

  // LSM303 Accel WHO_AM_I (reg 0x0F) = 0x33
  uint8_t accelAddrs[] = {0x19, 0x18};
  for (int i = 0; i < 2; i++) {
    uint8_t a = accelAddrs[i];
    Wire.beginTransmission(a);
    Wire.write(0x0F);
    if (Wire.endTransmission(false) == 0) {
      Wire.requestFrom((int)a, 1);
      if (Wire.available()) {
        uint8_t who = Wire.read();
        Serial.printf("  0x%02X WHO_AM_I: 0x%02X", a, who);
        if (who == 0x33) Serial.print("  -> LSM303 Accel CONFIRMADO");
        Serial.println();
      }
    } else {
      Serial.printf("  0x%02X no responde\n", a);
    }
  }

  // LSM303 Mag STATUS_REG_M (reg 0x07) en 0x1E - distingue de HMC5883
  Wire.beginTransmission(0x1E);
  Wire.write(0x07);
  if (Wire.endTransmission(false) == 0) {
    Wire.requestFrom((int)0x1E, 1);
    if (Wire.available()) {
      uint8_t sr = Wire.read();
      Serial.printf("  0x1E STATUS_REG_M (0x07): 0x%02X\n", sr);
    }
  }

  Serial.println("\nLoop: re-escaneo cada 5s. Conecta/desconecta sensores para verificar.");
}

void loop() {
  delay(5000);
  Serial.println("\n--- Re-escaneo + WHO_AM_I ---");
  scanOnce();

  // WHO_AM_I de 0x18 y 0x19 en cada ciclo para no perderlo
  uint8_t addrs[] = {0x18, 0x19};
  for (int i = 0; i < 2; i++) {
    uint8_t a = addrs[i];
    Wire.beginTransmission(a);
    Wire.write(0x0F);
    if (Wire.endTransmission(false) == 0) {
      Wire.requestFrom((int)a, 1);
      if (Wire.available()) {
        uint8_t who = Wire.read();
        Serial.printf("  0x%02X WHO_AM_I (reg 0x0F): 0x%02X", a, who);
        if (who == 0x33) Serial.print("  -> LSM303DLHC OK");
        else if (who == 0x40) Serial.print("  -> LSM303AGR (necesita otra libreria)");
        Serial.println();
      }
    }
  }
  // Dump primeros 16 registros de 0x18
  Serial.print("  Regs 0x20-0x27 de 0x18: ");
  for (uint8_t r = 0x20; r <= 0x27; r++) {
    Wire.beginTransmission(0x18);
    Wire.write(r);
    if (Wire.endTransmission(false) == 0) {
      Wire.requestFrom((int)0x18, 1);
      if (Wire.available()) Serial.printf("0x%02X ", Wire.read());
    }
  }
  Serial.println();
}
