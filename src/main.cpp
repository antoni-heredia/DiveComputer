#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_HMC5883_U.h>
#include <Adafruit_LSM303_Accel.h>

// Pines ST7789
#define TFT_CS   2
#define TFT_RST  0
#define TFT_DC   1
#define TFT_MOSI 8
#define TFT_SCLK 9

// Instanciamos la pantalla (CS, DC, RST, MOSI, SCLK)
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

// Magnetómetro
Adafruit_HMC5883_Unified mag = Adafruit_HMC5883_Unified(12345);

// Acelerometro
Adafruit_LSM303_Accel_Unified accel = Adafruit_LSM303_Accel_Unified(54321);
void setupSerial(void) {
  Serial.begin(115200);
  while (!Serial) { delay(10); }
}

void setupWire(void) {
  Wire.begin(6, 7);
}

void setupMagnetometer(void) {
  if (!mag.begin()) {
    Serial.println("Ooops, no HMC5883 detected ... Check your wiring!");
    while(1) { delay(100); }
  }
}

void setupTFT(void) {
  Serial.println("DEBUG: setupTFT() iniciado");
  
  // Inicializa SPI explícitamente para C3
  SPI.begin(9, -1, 8); // SCLK=9, MISO=-1 (no usado), MOSI=8
  Serial.println("DEBUG: SPI.begin() OK");
  
  // Inicializa la pantalla (altura, ancho)
  Serial.println("DEBUG: Iniciando tft.init()");
  tft.initR(INITR_BLACKTAB);
  Serial.println("DEBUG: tft.init() OK");
  
  tft.setRotation(0); // Modo horizontal
  Serial.println("DEBUG: setRotation OK");
  
  tft.fillScreen(ST77XX_BLACK);
  Serial.println("DEBUG: fillScreen OK");
  
  // Dibuja el puntero central estático en la pantalla física
  tft.fillTriangle(80, 15, 75, 5, 85, 5, ST77XX_RED);
  Serial.println("DEBUG: fillTriangle OK");
  
  Serial.println("DEBUG: setupTFT() completado exitosamente!");
}

float calculateHeading(sensors_event_t magEvent, sensors_event_t accEvent) {
  // 1. Calcular Pitch (rotación sobre eje X) y Roll (rotación sobre eje Y)
  // Nota: Estas fórmulas dependen de cómo esté orientado físicamente tu sensor.
  float pitch = atan2(-accEvent.acceleration.x, sqrt(accEvent.acceleration.y * accEvent.acceleration.y + accEvent.acceleration.z * accEvent.acceleration.z));
  float roll = atan2(accEvent.acceleration.y, accEvent.acceleration.z);

  // 2. Aplicar corrección de inclinación al magnetómetro
  // Xh e Yh son los ejes horizontales corregidos
  float magX = magEvent.magnetic.x;
  float magY = magEvent.magnetic.y;
  float magZ = magEvent.magnetic.z;

  float Xh = magX * cos(pitch) + magZ * sin(pitch);
  float Yh = magX * sin(roll) * sin(pitch) + magY * cos(roll) - magZ * sin(roll) * cos(pitch);

  // 3. Calcular el rumbo
  float heading = atan2(Yh, Xh);

  // 4. Declinación magnética y normalización
  float declinationAngle = -0.011; // Ajustar a tu ubicación
  heading += declinationAngle;

  if (heading < 0) heading += 2 * PI;
  if (heading > 2 * PI) heading -= 2 * PI;

  return heading * 180.0 / PI;
}
void setupAccelerometer(void) {
  if (!accel.begin()) {
    Serial.println("Ooops, no LSM303 Accel detected ... Check your wiring!");
    while(1) { delay(100); }
  }
}
void drawGameCompass(float heading) {
  // Configuración
  static float lastHeading = -999.0;
  int screenWidth = 128;
  int screenCenter = 64; 
  int fov = 90; 
  int yCompassLine = 40;
  int compassHeight = 50; // Altura del área de marcas

  // 1. Solo actualizar si el cambio es mayor a 1 grado (ajustar si es muy lento)
  if (abs(heading - lastHeading) < 1.0) return;
  lastHeading = heading;

  // 2. Limpiar SOLO el área de la brújula y el texto, no toda la pantalla
  tft.fillRect(0, yCompassLine, screenWidth, 80, ST77XX_BLACK);

  // 3. Dibujamos las marcas
  for (int i = 0; i < 360; i += 15) {
    int diff = i - (int)heading;
    if (diff < -180) diff += 360;
    if (diff > 180) diff -= 360;

    if (abs(diff) <= (fov / 2) + 15) {
      int x = screenCenter + (diff * (screenWidth / (float)fov));

      if (x >= 0 && x < screenWidth) {
        if (i % 90 == 0) { 
          tft.drawLine(x, yCompassLine, x, yCompassLine + 20, ST77XX_WHITE);
          tft.setTextColor(ST77XX_CYAN, ST77XX_BLACK);
          tft.setTextSize(2);
          tft.setCursor(x - 6, yCompassLine + 25);
          if (i == 0) tft.print("N");
          else if (i == 90) tft.print("E");
          else if (i == 180) tft.print("S");
          else if (i == 270) tft.print("W");
        } else if (i % 45 == 0) {
          tft.drawLine(x, yCompassLine, x, yCompassLine + 12, tft.color565(192, 192, 192));
        } else {
          tft.drawLine(x, yCompassLine, x, yCompassLine + 8, tft.color565(64, 64, 64));
        }
      }
    }
  }

  // 4. Ángulo numérico (borrado selectivo implícito en el fillRect superior)
  tft.setTextSize(3);
  String angleText = String((int)heading) + char(247);
  int16_t x1, y1; uint16_t w, h;
  tft.getTextBounds(angleText, 0, 0, &x1, &y1, &w, &h);
  tft.setCursor(screenCenter - (w / 2), 120);
  tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
  tft.print(angleText);
}

void setup(void) {
  setupSerial();
  
  Serial.println("1. Serial OK");
  delay(500);
  
  setupTFT();
  Serial.println("2. TFT OK");
  delay(500);

  setupWire();
  Serial.println("3. Wire (I2C) OK");
  delay(500);

  setupMagnetometer();
  Serial.println("4. Magnetometer OK");
  delay(500);
  setupAccelerometer(); // Si tienes un acelerómetro, inicialízalo aquí
  Serial.println("5. Accelerometer OK");
  Serial.println("Setup completado!");
}

void loop(void) {
  sensors_event_t event;
  sensors_event_t accEvent;
  accel.getEvent(&accEvent); // Obtener datos del acelerómetro
  mag.getEvent(&event);
 
  float headingDegrees = calculateHeading(event,accEvent);
  
  // Actualizar la interfaz gráfica
  drawGameCompass(headingDegrees);
  //paintScreenRed(); // Para probar que la pantalla se actualiza correctamente
  Serial.print("Heading: "); 
  Serial.println(headingDegrees);
  
  delay(30); 
}