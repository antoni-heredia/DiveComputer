#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_HMC5883_U.h>
#include <Adafruit_LSM303_Accel.h>
#include <SD.h>
#include <DNSServer.h>
#include <Adafruit_NeoPixel.h>
#include "deco.h"
#include "web.h"

// LED RGB integrado — GPIO 48 solo disponible en S3; C3 lo omite (GPIO 8 colisiona con TFT MOSI)
#if defined(CONFIG_IDF_TARGET_ESP32S3)
  #define HAS_RGB_LED 1
  #define RGB_LED_PIN 48
#else
  #define HAS_RGB_LED 0
#endif

#if HAS_RGB_LED
static Adafruit_NeoPixel rgbLed(1, RGB_LED_PIN, NEO_GRB + NEO_KHZ800);

// Estado para detección de bajada a velocidad constante
static float         s_ledDepthPrev  = 0.0f;
static float         s_ledVelPrev    = 0.0f;
static unsigned long s_ledSampleMs   = 0;
static bool          s_ledVelReady   = false;
static bool          s_constDescent  = false;

static void setupRGBLed(void) {
  rgbLed.begin();
  rgbLed.setBrightness(80);
  rgbLed.clear();
  rgbLed.show();
}

// Actualiza el LED RGB según profundidad y tasa de bajada.
// Prioridad 1: depth >= 30 m  → rojo fijo
// Prioridad 2: bajada constante → verde parpadeante (500 ms)
// Resto: apagado
static void updateRGBLed(float depth) {
  unsigned long now = millis();

  // Muestreo cada 2 s para estimar velocidad (m/s, positivo = bajando)
  if (now - s_ledSampleMs >= 2000UL) {
    float dt  = (now - s_ledSampleMs) / 1000.0f;
    float vel = (depth - s_ledDepthPrev) / dt;

    if (s_ledVelReady) {
      // Constante = ambas velocidades > 0.05 m/s y la variación < 40 % de la media
      float avg   = (fabsf(vel) + fabsf(s_ledVelPrev)) * 0.5f;
      bool  desc   = vel > 0.05f;
      bool  stable = desc && (fabsf(vel - s_ledVelPrev) < avg * 0.4f + 0.03f);
      s_constDescent = stable;
    } else {
      s_ledVelReady = true;
    }

    s_ledVelPrev   = vel;
    s_ledDepthPrev = depth;
    s_ledSampleMs  = now;
  }

  if (depth >= 30.0f) {
    rgbLed.setPixelColor(0, rgbLed.Color(255, 0, 0));
    rgbLed.show();
    return;
  }

  if (s_constDescent) {
    bool on = (now / 500UL) % 2 == 0;
    rgbLed.setPixelColor(0, on ? rgbLed.Color(0, 255, 0) : rgbLed.Color(0, 0, 0));
    rgbLed.show();
    return;
  }

  rgbLed.setPixelColor(0, rgbLed.Color(0, 0, 0));
  rgbLed.show();
}
#endif

// Pines ST7735
#define TFT_CS   11
#define TFT_RST  9
#define TFT_DC   10
#define TFT_MOSI 3
#define TFT_SCLK 4

// Pines microSD (comparte bus SPI con TFT)
#define SD_CS   6
#define SD_MISO 5

// Pines MS5837 (Wire1 — Core 0)
#define MS5837_SDA_PIN 8
#define MS5837_SCL_PIN 13

// Instanciamos la pantalla con hardware SPI (CS, DC, RST)
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

// Magnetómetro
Adafruit_HMC5883_Unified mag = Adafruit_HMC5883_Unified(12345);

// Acelerometro
Adafruit_LSM303_Accel_Unified accel = Adafruit_LSM303_Accel_Unified(54321);

// ---------------------------------------------------------------------------
// MS5837 driver directo sobre Wire1 (la librería BlueRobotics usa Wire global)
// Protocolo: datasheet TE Connectivity MS5837-30BA
// ---------------------------------------------------------------------------
#define MS5837_ADDR        0x76
#define MS5837_RESET       0x1E
#define MS5837_ADC_READ    0x00
#define MS5837_PROM_READ   0xA0
#define MS5837_CONV_D1     0x4A   // OSR=8192
#define MS5837_CONV_D2     0x5A   // OSR=8192

static uint16_t s_ms5837C[8];          // PROM calibration words
static bool     s_ms5837Ok = false;

static uint8_t ms5837_crc4(uint16_t prom[]) {
  uint16_t n_rem = 0;
  prom[0] = prom[0] & 0x0FFF;
  prom[7] = 0;
  for (uint8_t i = 0; i < 16; i++) {
    n_rem ^= (i % 2 == 1) ? (uint16_t)(prom[i>>1] & 0x00FF)
                           : (uint16_t)(prom[i>>1] >> 8);
    for (uint8_t b = 8; b > 0; b--)
      n_rem = (n_rem & 0x8000) ? (n_rem << 1) ^ 0x3000 : (n_rem << 1);
  }
  return (n_rem >> 12) & 0x000F;
}

static bool ms5837_init() {
  Wire1.beginTransmission(MS5837_ADDR);
  Wire1.write(MS5837_RESET);
  Wire1.endTransmission();  // sensor resetea sin ACK — ignorar retorno
  delay(10);
  memset(s_ms5837C, 0, sizeof(s_ms5837C));  // elemento [7] debe ser 0 para CRC
  for (uint8_t i = 0; i < 7; i++) {
    Wire1.beginTransmission(MS5837_ADDR);
    Wire1.write(MS5837_PROM_READ + i * 2);
    Wire1.endTransmission();
    Wire1.requestFrom((uint8_t)MS5837_ADDR, (uint8_t)2);
    s_ms5837C[i] = ((uint16_t)Wire1.read() << 8) | Wire1.read();
  }
  uint8_t crcRead = s_ms5837C[0] >> 12;       // leer ANTES de que crc4 borre el nibble
  uint8_t crcCalc = ms5837_crc4(s_ms5837C);
  return crcCalc == crcRead;
}

static void ms5837_read(float &depth_m, float &temp_c, float &bar) {
  auto readADC = [](uint8_t cmd) -> uint32_t {
    Wire1.beginTransmission(MS5837_ADDR);
    Wire1.write(cmd);
    Wire1.endTransmission();
    vTaskDelay(pdMS_TO_TICKS(20));
    Wire1.beginTransmission(MS5837_ADDR);
    Wire1.write(MS5837_ADC_READ);
    Wire1.endTransmission();
    Wire1.requestFrom((uint8_t)MS5837_ADDR, (uint8_t)3);
    return ((uint32_t)Wire1.read() << 16) |
           ((uint32_t)Wire1.read() << 8)  |
            (uint32_t)Wire1.read();
  };

  uint32_t D1 = readADC(MS5837_CONV_D1);
  uint32_t D2 = readADC(MS5837_CONV_D2);

  // First-order (30BA model)
  int32_t  dT   = (int32_t)D2 - (uint32_t)s_ms5837C[5] * 256L;
  int64_t  SENS = (int64_t)s_ms5837C[1] * 32768LL + (int64_t)s_ms5837C[3] * dT / 256LL;
  int64_t  OFF  = (int64_t)s_ms5837C[2] * 65536LL + (int64_t)s_ms5837C[4] * dT / 128LL;
  int32_t  P    = (int32_t)((D1 * SENS / 2097152LL - OFF) / 8192LL);
  int32_t  TEMP = 2000L + (int64_t)dT * s_ms5837C[6] / 8388608LL;

  // Second-order compensation (low temp)
  if (TEMP / 100 < 20) {
    int32_t Ti    = (int32_t)(3LL * dT * dT / 8589934592LL);
    int64_t OFFi  = 3LL * (TEMP - 2000) * (TEMP - 2000) / 2;
    int64_t SENSi = 5LL * (TEMP - 2000) * (TEMP - 2000) / 8;
    TEMP -= Ti;
    OFF  -= OFFi;
    SENS -= SENSi;
    P = (int32_t)((D1 * SENS / 2097152LL - OFF) / 8192LL);
  }

  temp_c  = TEMP / 100.0f;
  float pressure_Pa = P * 10.0f;  // mbar × 100 → Pa
  bar     = pressure_Pa / 100000.0f;
  depth_m = (pressure_Pa - 101300.0f) / (1025.0f * 9.80665f);
  if (depth_m < 0.0f) depth_m = 0.0f;
}

// ---------------------------------------------------------------------------
// Calibración del magnetómetro (hard-iron + soft-iron) + portal WiFi
// ---------------------------------------------------------------------------
const char* AP_SSID = "Brujula-Calib";
const char* AP_PASS = "";   // red abierta para pruebas; pon "brujula123" para proteger

struct Calibration {
  float ox = 0, oy = 0, oz = 0;   // offsets hard-iron (uT)
  float sx = 1, sy = 1, sz = 1;   // escalas soft-iron
} cal;

struct CalState {
  bool  collecting = false;
  long  samples = 0;
  float minX = 0, minY = 0, minZ = 0;
  float maxX = 0, maxY = 0, maxZ = 0;
} calState;

float g_heading = 0;            // último rumbo calculado (lo lee el portal)

// Datos de buceo compartidos entre Core 0 (escribe) y Core 1 (lee/dibuja)
float          g_depth      = 0.0f;
float          g_tempC      = 24.5f;
unsigned long  g_diveStartMs = 0;
static SemaphoreHandle_t g_dataMutex = nullptr;
static DecoResult        g_deco      = {0.0f, 999.0f, false};

static bool          s_sdOk      = false;
static unsigned long s_lastLogMs = 0;

// ── Gestión de sesiones de buceo ──────────────────────────────
#define DIVE_THRESH      0.5f      // m — umbral inicio/fin de sesión
#define SURFACE_GRACE_MS 15000UL   // ms en superficie antes de cerrar sesión

static uint8_t       s_sessId      = 0;
static bool          s_inDive      = false;
static unsigned long s_sessStartMs = 0;
static unsigned long s_surfaceMs   = 0;

static void updateDiveSession(float depth) {
  if (!s_inDive) {
    if (depth >= DIVE_THRESH) {
      s_surfaceMs   = 0;
      s_sessId++;
      s_inDive      = true;
      s_sessStartMs = millis();
      s_lastLogMs   = 0;  // forzar log inmediato
      if (s_sdOk) {
        File f = SD.open("/dive.csv", FILE_APPEND);
        if (f) { f.printf("#SESSION %u\n", s_sessId); f.close(); }
      }
    }
  } else {
    if (depth < DIVE_THRESH) {
      if (s_surfaceMs == 0) s_surfaceMs = millis();
      else if (millis() - s_surfaceMs >= SURFACE_GRACE_MS) {
        s_inDive    = false;
        s_surfaceMs = 0;
      }
    } else {
      s_surfaceMs = 0;
    }
  }
}

Preferences prefs;
WebServer   server(80);
DNSServer   dnsServer;

void loadCalibration(void) {
  prefs.begin("compass", true);
  cal.ox = prefs.getFloat("ox", 0); cal.oy = prefs.getFloat("oy", 0); cal.oz = prefs.getFloat("oz", 0);
  cal.sx = prefs.getFloat("sx", 1); cal.sy = prefs.getFloat("sy", 1); cal.sz = prefs.getFloat("sz", 1);
  prefs.end();
  Serial.printf("Calibracion cargada: off=(%.2f,%.2f,%.2f) scale=(%.3f,%.3f,%.3f)\n",
                cal.ox, cal.oy, cal.oz, cal.sx, cal.sy, cal.sz);
}

void saveCalibration(void) {
  prefs.begin("compass", false);
  prefs.putFloat("ox", cal.ox); prefs.putFloat("oy", cal.oy); prefs.putFloat("oz", cal.oz);
  prefs.putFloat("sx", cal.sx); prefs.putFloat("sy", cal.sy); prefs.putFloat("sz", cal.sz);
  prefs.end();
}

// PAGE_HTML se define en web.h (portal completo Bitácora de Buceo)

void handleRoot(void)  { server.send_P(200, "text/html", PAGE_HTML); }

void handleData(void) {
  if (!s_sdOk) { server.send(503, "text/plain", "SD no disponible"); return; }
  File f = SD.open("/dive.csv");
  if (!f)      { server.send(404, "text/plain", "Sin datos");        return; }
  server.streamFile(f, "text/csv");
  f.close();
}

void handleClearLog(void) {
  if (s_sdOk) {
    SD.remove("/dive.csv");
    SD.remove("/meta.txt");
    File f = SD.open("/dive.csv", FILE_WRITE);
    if (f) { f.println("sess,t_s,prof_m,temp_c,rumbo_deg"); f.close(); }
    s_sessId = 0;
    s_inDive = false;
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

// ---------------------------------------------------------------------------
// /api/dives — convierte dive.csv (multi-sesión) en JSON para el portal
//
// Formato CSV: sess,t_s,prof_m,temp_c,rumbo_deg  (cabecera)
//              #SESSION N  (marcador de inicio de sesión)
//              N,t_s,prof_m,temp_c,rumbo_deg  (datos)
// Metadatos por sesión: /meta.txt  →  "id|location|starttime\n"
// ---------------------------------------------------------------------------
struct SessInfo {
  uint8_t  id;
  uint32_t fileOff;       // offset después del marcador #SESSION (primer dato)
  float    maxDepth, minDepth, sumDepth;
  float    maxTemp,  minTemp, sumHeading;
  float    maxAscent, prevDepth;
  unsigned long prevT, lastT;
  long     rows;
};

// Formato /meta.txt: id|nombre_sitio|lat|lng|starttime
struct DiveMeta { uint8_t id; char loc[64]; char lat[16]; char lng[16]; char ts[32]; };

static void readMeta(DiveMeta* meta, int& nmeta, int maxn) {
  nmeta = 0;
  File fm = SD.open("/meta.txt");
  if (!fm) return;
  while (fm.available() && nmeta < maxn) {
    String line = fm.readStringUntil('\n');
    line.trim();
    if (!line.length()) continue;
    int p1 = line.indexOf('|');
    int p2 = p1 >= 0 ? line.indexOf('|', p1+1) : -1;
    int p3 = p2 >= 0 ? line.indexOf('|', p2+1) : -1;
    int p4 = p3 >= 0 ? line.indexOf('|', p3+1) : -1;
    if (p1 < 0) continue;
    meta[nmeta].id = (uint8_t)line.substring(0, p1).toInt();
    line.substring(p1+1, p2 >= 0 ? p2 : (int)line.length()).toCharArray(meta[nmeta].loc, 64);
    if (p2 >= 0) line.substring(p2+1, p3 >= 0 ? p3 : (int)line.length()).toCharArray(meta[nmeta].lat, 16);
    else meta[nmeta].lat[0] = '\0';
    if (p3 >= 0) line.substring(p3+1, p4 >= 0 ? p4 : (int)line.length()).toCharArray(meta[nmeta].lng, 16);
    else meta[nmeta].lng[0] = '\0';
    if (p4 >= 0) line.substring(p4+1).toCharArray(meta[nmeta].ts, 32);
    else meta[nmeta].ts[0] = '\0';
    nmeta++;
  }
  fm.close();
}

void handleDives(void) {
  server.sendHeader("Cache-Control", "no-cache");
  server.sendHeader("Access-Control-Allow-Origin", "*");

  if (!s_sdOk) { server.send(200, "application/json", F("{\"dives\":[]}")); return; }

  File f = SD.open("/dive.csv");
  if (!f || f.size() < 20) {
    if (f) f.close();
    server.send(200, "application/json", F("{\"dives\":[]}"));
    return;
  }

  // ── Pasada 1: estadísticas por sesión ──────────────────────────
  static SessInfo sess[20];
  int nsess = 0, cur = -1;

  f.readStringUntil('\n');  // cabecera

  while (f.available() && nsess < 20) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (!line.length()) continue;

    if (line.startsWith(F("#SESSION"))) {
      cur = nsess++;
      sess[cur].id       = (uint8_t)line.substring(9).toInt();
      sess[cur].fileOff  = (uint32_t)f.position();
      sess[cur].maxDepth = 0;    sess[cur].minDepth = 9999;
      sess[cur].sumDepth = 0;    sess[cur].sumHeading = 0;
      sess[cur].maxTemp  = -99;  sess[cur].minTemp = 99;
      sess[cur].maxAscent = 0;   sess[cur].prevDepth = 0;
      sess[cur].prevT = 0;       sess[cur].lastT = 0;
      sess[cur].rows = 0;
      continue;
    }
    if (cur < 0) continue;

    // formato: sess,t_s,prof_m,temp_c,rumbo_deg
    int c1 = line.indexOf(',');
    int c2 = c1 >= 0 ? line.indexOf(',', c1+1) : -1;
    int c3 = c2 >= 0 ? line.indexOf(',', c2+1) : -1;
    int c4 = c3 >= 0 ? line.indexOf(',', c3+1) : -1;
    if (c4 < 0) continue;

    unsigned long t = (unsigned long)line.substring(c1+1, c2).toInt();
    float dep = line.substring(c2+1, c3).toFloat();
    float tmp = line.substring(c3+1, c4).toFloat();
    float hdg = line.substring(c4+1).toFloat();

    SessInfo& s = sess[cur];
    if (dep > s.maxDepth) s.maxDepth = dep;
    if (dep > 0.3f && dep < s.minDepth) s.minDepth = dep;
    if (tmp > s.maxTemp) s.maxTemp = tmp;
    if (tmp < s.minTemp) s.minTemp = tmp;
    s.sumDepth   += dep;
    s.sumHeading += hdg;
    s.lastT       = t;
    if (s.rows > 0 && t > s.prevT) {
      float dtMin = (float)(t - s.prevT) / 60.0f;
      if (dtMin > 0.001f) {
        float rate = (s.prevDepth - dep) / dtMin;
        if (rate > s.maxAscent) s.maxAscent = rate;
      }
    }
    s.prevDepth = dep;
    s.prevT = t;
    s.rows++;
  }
  f.close();

  if (nsess == 0) { server.send(200, "application/json", F("{\"dives\":[]}")); return; }

  // ── Leer metadatos (/meta.txt) ─────────────────────────────────
  static DiveMeta meta[20];
  int nmeta = 0;
  readMeta(meta, nmeta, 20);
  auto getMeta = [&](uint8_t id) -> DiveMeta* {
    for (int i = 0; i < nmeta; i++) if (meta[i].id == id) return &meta[i];
    return nullptr;
  };

  // ── Respuesta chunked ──────────────────────────────────────────
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  server.sendContent(F("{\"dives\":["));

  char buf[192];
  bool firstDive = true;

  for (int si = 0; si < nsess; si++) {
    SessInfo& s = sess[si];
    if (s.rows == 0) continue;
    if (s.minDepth > s.maxDepth) s.minDepth = 0;

    DiveMeta* m = getMeta(s.id);
    const char* loc = (m && m->loc[0]) ? m->loc : "";
    const char* ts  = (m && m->ts[0])  ? m->ts  : "";
    float avgDepth   = s.sumDepth   / (float)s.rows;
    float avgHeading = s.sumHeading / (float)s.rows;
    float durMin     = (float)s.lastT / 60.0f;

    if (!firstDive) server.sendContent(F(","));
    firstDive = false;

    snprintf(buf, sizeof(buf), "{\"id\":\"d-%03u\",\"site\":\"Inmersi\xC3\xB3n %u\",", s.id, s.id);
    server.sendContent(buf);
    snprintf(buf, sizeof(buf), "\"location\":\"%s\",\"startTime\":\"%s\",\"date\":\"%s\",",
             loc, ts, ts[0] ? ts : "----");
    server.sendContent(buf);
    snprintf(buf, sizeof(buf),
      "\"durationMin\":%.1f,\"maxDepth\":%.1f,\"avgDepth\":%.1f,\"minDepth\":%.1f,",
      durMin, s.maxDepth, avgDepth, s.minDepth);
    server.sendContent(buf);
    snprintf(buf, sizeof(buf),
      "\"tempMax\":%.1f,\"tempMin\":%.1f,\"avgHeading\":%.1f,",
      s.maxTemp > -99 ? s.maxTemp : 0.0f,
      s.minTemp <  99 ? s.minTemp : 0.0f,
      avgHeading);
    server.sendContent(buf);
    snprintf(buf, sizeof(buf), "\"visibility\":0,\"gas\":\"Aire\",\"maxAscent\":%.1f,", s.maxAscent);
    server.sendContent(buf);
    server.sendContent(F("\"hasDeco\":false,\"safetyStop\":{\"depth\":5,\"minutes\":3},\"deco\":null,"));
    server.sendContent(F("\"samples\":["));

    // ── muestras de esta sesión ────────────────────────────────
    File f2 = SD.open("/dive.csv");
    if (f2) {
      f2.seek(s.fileOff);
      bool firstRow = true;
      while (f2.available()) {
        String line = f2.readStringUntil('\n');
        line.trim();
        if (line.startsWith(F("#SESSION"))) break;
        if (line.length() < 4) continue;
        int c1 = line.indexOf(',');
        int c2 = c1 >= 0 ? line.indexOf(',', c1+1) : -1;
        int c3 = c2 >= 0 ? line.indexOf(',', c2+1) : -1;
        int c4 = c3 >= 0 ? line.indexOf(',', c3+1) : -1;
        if (c4 < 0) continue;
        float tMin = line.substring(c1+1, c2).toFloat() / 60.0f;
        float dep  = line.substring(c2+1, c3).toFloat();
        float tmp  = line.substring(c3+1, c4).toFloat();
        int   hdg  = line.substring(c4+1).toInt();
        snprintf(buf, sizeof(buf),
          "%s{\"t\":%.2f,\"depth\":%.1f,\"temp\":%.1f,\"heading\":%d}",
          firstRow ? "" : ",", tMin, dep, tmp, hdg);
        server.sendContent(buf);
        firstRow = false;
      }
      f2.close();
    }
    server.sendContent(F("]}"));
  }

  server.sendContent(F("]}"));
  server.sendContent("");
}

// ---------------------------------------------------------------------------
// /api/meta  GET → JSON array de metadatos  POST → actualiza una sesión
// Params POST: sess=N&location=xxx&starttime=YYYY-MM-DDTHH:MM
// ---------------------------------------------------------------------------
void handleMeta(void) {
  server.sendHeader("Access-Control-Allow-Origin", "*");

  if (server.method() == HTTP_GET) {
    static DiveMeta meta[20]; int nmeta = 0;
    readMeta(meta, nmeta, 20);
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "application/json", "");
    server.sendContent(F("["));
    for (int i = 0; i < nmeta; i++) {
      char buf[128];
      snprintf(buf, sizeof(buf), "%s{\"id\":%u,\"location\":\"%s\",\"startTime\":\"%s\"}",
               i ? "," : "", meta[i].id, meta[i].loc, meta[i].ts);
      server.sendContent(buf);
    }
    server.sendContent(F("]"));
    server.sendContent("");
    return;
  }

  // POST: leer, modificar, reescribir /meta.txt
  String sessStr = server.arg("sess");
  if (!sessStr.length() || !s_sdOk) { server.send(400, "application/json", F("{\"ok\":false}")); return; }
  uint8_t targetId = (uint8_t)sessStr.toInt();
  String newLoc    = server.arg("location");
  String newTs     = server.arg("starttime");

  static DiveMeta entries[20]; int n = 0;
  readMeta(entries, n, 20);

  bool found = false;
  for (int i = 0; i < n; i++) {
    if (entries[i].id == targetId) {
      newLoc.toCharArray(entries[i].loc, 64);
      newTs.toCharArray(entries[i].ts, 32);
      found = true; break;
    }
  }
  if (!found && n < 20) {
    entries[n].id = targetId;
    newLoc.toCharArray(entries[n].loc, 64);
    newTs.toCharArray(entries[n].ts, 32);
    n++;
  }

  SD.remove("/meta.txt");
  File fw = SD.open("/meta.txt", FILE_WRITE);
  if (fw) {
    for (int i = 0; i < n; i++)
      fw.printf("%u|%s|%s\n", entries[i].id, entries[i].loc, entries[i].ts);
    fw.close();
  }
  server.send(200, "application/json", F("{\"ok\":true}"));
}

void handleStart(void) {
  calState.collecting = true;
  calState.samples = 0;
  calState.minX = calState.minY = calState.minZ =  1e9;
  calState.maxX = calState.maxY = calState.maxZ = -1e9;
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleStop(void) {
  calState.collecting = false;
  if (calState.samples > 50) {            // sólo si se recogieron datos suficientes
    float cx = (calState.maxX - calState.minX) / 2.0f;
    float cy = (calState.maxY - calState.minY) / 2.0f;
    float cz = (calState.maxZ - calState.minZ) / 2.0f;
    cal.ox = (calState.maxX + calState.minX) / 2.0f;
    cal.oy = (calState.maxY + calState.minY) / 2.0f;
    cal.oz = (calState.maxZ + calState.minZ) / 2.0f;
    float avg = (cx + cy + cz) / 3.0f;    // radio medio -> escalas soft-iron
    cal.sx = (cx > 0.1f) ? avg / cx : 1.0f;
    cal.sy = (cy > 0.1f) ? avg / cy : 1.0f;
    cal.sz = (cz > 0.1f) ? avg / cz : 1.0f;
    saveCalibration();
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleReset(void) {
  cal = Calibration();                    // offsets=0, escalas=1
  saveCalibration();
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleStatus(void) {
  DecoResult deco;
  float depth;
  if (xSemaphoreTake(g_dataMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    deco  = g_deco;
    depth = g_depth;
    xSemaphoreGive(g_dataMutex);
  } else {
    deco  = g_deco;
    depth = g_depth;
  }

  char buf[512];
  snprintf(buf, sizeof(buf),
    "{\"state\":\"%s\",\"samples\":%ld,"
    "\"min\":[%.1f,%.1f,%.1f],\"max\":[%.1f,%.1f,%.1f],"
    "\"offset\":[%.2f,%.2f,%.2f],\"scale\":[%.3f,%.3f,%.3f],"
    "\"heading\":%.1f,"
    "\"depth\":%.1f,\"ndl\":%.0f,\"ceiling\":%.1f,\"in_deco\":%s}",
    calState.collecting ? "calibrating" : "idle", calState.samples,
    calState.minX, calState.minY, calState.minZ,
    calState.maxX, calState.maxY, calState.maxZ,
    cal.ox, cal.oy, cal.oz, cal.sx, cal.sy, cal.sz, g_heading,
    depth, deco.ndl_min, deco.ceiling_m, deco.in_deco ? "true" : "false");
  server.send(200, "application/json", buf);
}

void setupWiFi(void) {
  WiFi.persistent(false);
  WiFi.disconnect(true);
  delay(200);
  WiFi.mode(WIFI_AP);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  WiFi.setSleep(false);
  delay(200);
  bool ok = WiFi.softAP(AP_SSID, strlen(AP_PASS) ? AP_PASS : nullptr, 1);  // canal 1
  Serial.print("softAP result: "); Serial.println(ok ? "OK" : "FALLO");
  Serial.print("AP \""); Serial.print(AP_SSID);
  Serial.print("\" -> http://"); Serial.println(WiFi.softAPIP());
  Serial.print("MAC: "); Serial.println(WiFi.softAPmacAddress());
  server.on("/",          handleRoot);
  server.on("/start",     handleStart);
  server.on("/stop",      handleStop);
  server.on("/reset",     handleReset);
  server.on("/status",    handleStatus);
  server.on("/data",      handleData);
  server.on("/clearlog",  handleClearLog);
  server.on("/api/dives", handleDives);
  server.on("/api/meta",  handleMeta);

  // Captive portal: redirigir detección de conectividad de cada SO a la página principal
  auto redir = []() {
    server.sendHeader("Location", "http://192.168.4.1/");
    server.send(302, "text/plain", "");
  };
  server.on("/generate_204",             redir);  // Android Chrome
  server.on("/gen_204",                  redir);  // Android alternativo
  server.on("/hotspot-detect.html",      redir);  // iOS / macOS
  server.on("/library/test/success.html",redir);  // macOS Safari
  server.on("/redirect",                 redir);  // Windows 11
  server.on("/connecttest.txt", []() { server.send(200,"text/plain","Microsoft Connect Test"); });
  server.on("/ncsi.txt",        []() { server.send(200,"text/plain","Microsoft NCSI"); });
  server.onNotFound(redir);

  server.begin();

  // DNS: responde cualquier dominio con la IP del AP → fuerza el captive portal
  dnsServer.start(53, "*", WiFi.softAPIP());
}

void setupSerial(void) {
  Serial.begin(115200);
  unsigned long t0 = millis();
  while (!Serial && (millis() - t0 < 2000)) { delay(10); }  // máx 2 s (no bloquea a batería)
}

void setupWire(void) {
  Wire.begin(I2C_SDA, I2C_SCL);   // pines definidos en platformio.ini
}

void setupMagnetometer(void) {
  if (!mag.begin()) {
    Serial.println("Ooops, no HMC5883 detected ... Check your wiring!");
    while(1) { delay(100); }
  }
}

// ---------------------------------------------------------------------------
// Layout de pantalla (rotation=1 → 160×128 landscape)
//
//  y  0..15   Heading strip: grados + cardinal
//  y 16..77   Zona profundidad (número grande)
//  y 78..95   Zona datos: tiempo inmersión + temperatura
//  y 96..127  Barra brújula (32 px)
// ---------------------------------------------------------------------------
#define SCR_W      160
#define SCR_H      128
#define Y_HDG_H     16
#define Y_DEPTH_TOP 16
#define Y_DEPTH_H   62
#define Y_DATA_TOP  78
#define Y_DATA_H    18
#define Y_CPASS_TOP 96
#define Y_CPASS_H   32

// g_depth, g_tempC, g_diveStartMs, g_dataMutex, g_deco — declarados al principio del archivo

// Estado de último renderizado para redibujado selectivo
static int   s_lastHdgInt      = -1;
static int   s_lastDepthInt    = -1;
static int   s_lastTempInt     = -1;
static int   s_lastNdlInt      = -9999;   // NDL en segundos enteros
static int   s_lastCeilInt     = -1;      // techo en décimas de metro
static int   s_lastElapsedSec  = -1;      // tiempo de inmersión en segundos

static const uint16_t COL_DIM  = 0x4208; // gris oscuro ~(64,64,64)
static const uint16_t COL_MID  = 0xB596; // gris medio ~(180,180,180)
static const uint16_t COL_ORG  = 0xFB00; // naranja ~(255,96,0)

static const char* headingToCardinal(float h) {
  int d = (int)h;
  if (d < 23 || d >= 338) return "N";
  if (d < 68)  return "NE";
  if (d < 113) return "E";
  if (d < 158) return "SE";
  if (d < 203) return "S";
  if (d < 248) return "SO";
  if (d < 293) return "O";
  return "NO";
}

// Dibuja la tira de rumbo (top 16 px) — solo si el rumbo cambió ≥1°
static void drawHeadingStrip(float heading) {
  int hInt = (int)heading;
  if (hInt == s_lastHdgInt) return;
  s_lastHdgInt = hInt;

  // Grados a la izquierda (fijo 4 chars "NNN°" → ancho constante)
  char buf[6];
  snprintf(buf, sizeof(buf), "%3d%c", hInt, char(247));
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_CYAN, ST77XX_BLACK);
  tft.setCursor(2, 4);
  tft.print(buf);

  // Cardinal centrado (textSize=2, max 2 chars "NE"=24 px → zona 60..100)
  const char* card = headingToCardinal(heading);
  tft.fillRect(55, 0, 50, Y_HDG_H, ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  int16_t bx, by; uint16_t bw, bh;
  tft.getTextBounds(card, 0, 0, &bx, &by, &bw, &bh);
  tft.setCursor((SCR_W - (int)bw) / 2, 1);
  tft.print(card);
}

// Dibuja profundidad grande — solo si cambió ≥0.1 m
static void drawDepth(float depth) {
  int dInt = (int)(depth * 10.0f);
  if (dInt == s_lastDepthInt) return;
  s_lastDepthInt = dInt;

  // Formato fijo 5 chars "%5.1f" → siempre 5×24=120 px a textSize=4
  char num[8];
  snprintf(num, sizeof(num), "%5.1f", depth);

  const int CHAR_W4 = 6 * 4; // 24 px por char a textSize=4
  const int NUM_W   = 5 * CHAR_W4; // 120 px
  const int UNIT_W  = 6 * 2;       // 12 px "m" a textSize=2
  const int TOTAL_W = NUM_W + 4 + UNIT_W;
  int startX = (SCR_W - TOTAL_W) / 2;
  int numY   = Y_DEPTH_TOP + (Y_DEPTH_H - 32) / 2 + 6; // vertically centred in zone

  // Etiqueta estática "PROF" (solo sobrescribe, el fondo cubre)
  tft.setTextSize(1);
  tft.setTextColor(COL_MID, ST77XX_BLACK);
  tft.setCursor(4, Y_DEPTH_TOP + 3);
  tft.print("PROF");

  // Número grande con fondo negro → sin parpadeo
  tft.setTextSize(4);
  tft.setTextColor(ST77XX_GREEN, ST77XX_BLACK);
  tft.setCursor(startX, numY);
  tft.print(num);

  // Unidad "m" más pequeña
  tft.setTextSize(2);
  tft.setTextColor(tft.color565(0, 200, 100), ST77XX_BLACK);
  tft.setCursor(startX + NUM_W + 4, numY + (32 - 16) / 2);
  tft.print("m");
}

// Dibuja tiempo de inmersión MM:SS en la esquina superior derecha de la zona de profundidad
// Actualiza solo cuando cambia el segundo — mismo nivel que la etiqueta "PROF"
static void drawElapsed(unsigned long elapsedMs) {
  int sec = (int)(elapsedMs / 1000UL);
  if (sec == s_lastElapsedSec) return;
  s_lastElapsedSec = sec;
  int mm = sec / 60;
  int ss = sec % 60;
  char buf[7];
  if (mm >= 100) {
    snprintf(buf, sizeof(buf), ">99:59");
  } else {
    snprintf(buf, sizeof(buf), "%02d:%02d", mm, ss);
  }
  tft.setTextSize(1);
  tft.setTextColor(COL_MID, ST77XX_BLACK);
  // 6 chars × 6 px = 36 px — alineado a la derecha
  tft.setCursor(SCR_W - 6 * 6 - 4, Y_DEPTH_TOP + 3);
  tft.print(buf);
}

// Dibuja zona de datos (NDL / techo de deco + temperatura) — redibuja solo si cambia
// Zona izquierda:  NDL en minutos (verde) o "DECO" (rojo) si hay obligación de parada
// Zona derecha:    techo en metros (rojo) si en deco; temperatura (naranja) si libre
static void drawDiveData(float tempC, DecoResult deco) {
  const int Y = Y_DATA_TOP + 5;

  // --- Izquierda: NDL MM:SS o DECO ---
  int ndlSec = deco.in_deco ? -1 : (int)(deco.ndl_min * 60.0f);
  if (ndlSec != s_lastNdlInt) {
    s_lastNdlInt = ndlSec;
    tft.setTextSize(1);
    char buf[12];
    if (deco.in_deco) {
      tft.setTextColor(ST77XX_RED, ST77XX_BLACK);
      tft.setCursor(4, Y);
      tft.print("DECO    ");  // 8 chars — cubre el ancho de "NDL99:59"
    } else {
      int mm = ndlSec / 60;
      int ss = ndlSec % 60;
      if (mm >= 100) {
        snprintf(buf, sizeof(buf), "NDL>99m ");  // 8 chars fijos
      } else {
        snprintf(buf, sizeof(buf), "NDL%02d:%02d", mm, ss);
      }
      tft.setTextColor(mm < 5 ? ST77XX_YELLOW : ST77XX_GREEN, ST77XX_BLACK);
      tft.setCursor(4, Y);
      tft.print(buf);
    }
  }

  // --- Derecha: techo o temperatura ---
  int ceilInt = (int)(deco.ceiling_m * 10.0f);
  int tempInt = (int)(tempC * 10.0f);
  bool rightChanged = deco.in_deco ? (ceilInt != s_lastCeilInt)
                                   : (tempInt != s_lastTempInt);
  if (rightChanged) {
    s_lastCeilInt = ceilInt;
    s_lastTempInt = tempInt;
    tft.setTextSize(1);
    char buf[10];
    if (deco.in_deco) {
      snprintf(buf, sizeof(buf), "CEI%4.1fm", deco.ceiling_m);
      tft.setTextColor(ST77XX_RED, ST77XX_BLACK);
    } else {
      snprintf(buf, sizeof(buf), "%5.1f%cC", tempC, char(247));
      tft.setTextColor(COL_ORG, ST77XX_BLACK);
    }
    // 8 chars × 6 px = 48 px → alinear a la derecha
    tft.setCursor(SCR_W - 8 * 6 - 4, Y);
    tft.print(buf);
  }
}

// Dibuja la barra de brújula inferior — solo si rumbo cambió
static void drawCompassBar(float heading) {
  const int barY  = Y_CPASS_TOP;
  const int barH  = Y_CPASS_H;
  const int cX    = SCR_W / 2;
  const int fov   = 90;

  tft.fillRect(0, barY, SCR_W, barH, ST77XX_BLACK);

  // Puntero central fijo (triángulo rojo apuntando hacia abajo)
  tft.fillTriangle(cX, barY + 2, cX - 5, barY + 11, cX + 5, barY + 11, ST77XX_RED);

  for (int i = 0; i < 360; i += 15) {
    int diff = i - (int)heading;
    if (diff < -180) diff += 360;
    if (diff >  180) diff -= 360;
    if (abs(diff) > fov / 2 + 15) continue;

    int x = cX + diff * SCR_W / fov;
    if (x < 0 || x >= SCR_W) continue;

    if (i % 90 == 0) {
      tft.drawLine(x, barY + 12, x, barY + 23, ST77XX_WHITE);
      tft.setTextSize(1);
      tft.setTextColor(ST77XX_CYAN, ST77XX_BLACK);
      const char* lbl = (i == 0) ? "N" : (i == 90) ? "E" : (i == 180) ? "S" : "W";
      tft.setCursor(x - 3, barY + 24);
      tft.print(lbl);
    } else if (i % 45 == 0) {
      tft.drawLine(x, barY + 14, x, barY + 23, COL_MID);
    } else {
      tft.drawLine(x, barY + 17, x, barY + 23, COL_DIM);
    }
  }
}

static void drawFrame(void) {
  uint16_t lineCol = tft.color565(40, 40, 40);
  tft.drawFastHLine(0, Y_HDG_H,     SCR_W, lineCol);
  tft.drawFastHLine(0, Y_DATA_TOP,  SCR_W, lineCol);
  tft.drawFastHLine(0, Y_CPASS_TOP, SCR_W, lineCol);
}

static void drawAll(float heading, float depth, float tempC, DecoResult deco) {
  drawHeadingStrip(heading);
  drawDepth(depth);
  drawDiveData(tempC, deco);
  drawCompassBar(heading);
}

static void showSplash(void) {
  tft.fillScreen(ST77XX_BLACK);

  // Bandera de buceo: rectángulo rojo con franja blanca diagonal (sup-izq → inf-der)
  const int FX = 40, FY = 10, FW = 80, FH = 50, ST = 10;
  tft.fillRect(FX, FY, FW, FH, ST77XX_RED);
  // Franja blanca: quad (FX,FY)-(FX+ST,FY)-(FX+FW,FY+FH)-(FX+FW-ST,FY+FH)
  tft.fillTriangle(FX, FY, FX+ST, FY, FX+FW-ST, FY+FH, ST77XX_WHITE);
  tft.fillTriangle(FX, FY, FX+FW-ST, FY+FH, FX+FW, FY+FH, ST77XX_WHITE);

  // Nombre centrado
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  int16_t bx, by; uint16_t bw, bh;
  tft.getTextBounds("Toni Heredia", 0, 0, &bx, &by, &bw, &bh);
  tft.setCursor((SCR_W - (int)bw) / 2, 72);
  tft.print("Toni Heredia");

  delay(2500);
  tft.fillScreen(ST77XX_BLACK);
}

void setupSD(void) {
  if (!SD.begin(SD_CS)) {
    Serial.println("SD: no detectada o fallo init");
    return;
  }
  s_sdOk = true;
  // Detectar formato antiguo (sin columna sess) y reinicializar si es necesario
  bool needInit = !SD.exists("/dive.csv");
  if (!needInit) {
    File fc = SD.open("/dive.csv");
    if (fc) {
      String hdr = fc.readStringUntil('\n');
      fc.close();
      if (!hdr.startsWith("sess")) needInit = true;  // formato viejo
    }
  }
  if (needInit) {
    SD.remove("/dive.csv");
    SD.remove("/meta.txt");
    File f = SD.open("/dive.csv", FILE_WRITE);
    if (f) { f.println("sess,t_s,prof_m,temp_c,rumbo_deg"); f.close(); }
    Serial.println("SD: archivo inicializado con nuevo formato");
  }
  Serial.println("SD OK");
}

void logToSD(float depth, float tempC, float heading) {
  if (!s_sdOk || !s_inDive) return;
  if (millis() - s_lastLogMs < 5000UL) return;
  s_lastLogMs = millis();
  File f = SD.open("/dive.csv", FILE_APPEND);
  if (!f) return;
  char line[56];
  unsigned long t_s = (millis() - s_sessStartMs) / 1000UL;
  snprintf(line, sizeof(line), "%u,%lu,%.1f,%.1f,%.1f", s_sessId, t_s, depth, tempC, heading);
  f.println(line);
  f.close();
}

void setupTFT(void) {
  SPI.begin(TFT_SCLK, SD_MISO, TFT_MOSI);
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(1);   // landscape: 160×128
  tft.fillScreen(ST77XX_BLACK);
  showSplash();
  drawFrame();
  Serial.println("DEBUG: setupTFT() OK (landscape 160x128)");
}

float calculateHeading(sensors_event_t magEvent, sensors_event_t accEvent) {
  // Montaje físico: sensor-X=abajo, sensor-Y=izquierda, sensor-Z=adelante
  // Remapeo a marco de fórmula: X=adelante, Y=izquierda, Z=arriba
  float ax = -accEvent.acceleration.z;
  float ay =  accEvent.acceleration.y;
  float az = -accEvent.acceleration.x;

  // Calibración en marco del sensor, luego remapeo
  float mx = -(magEvent.magnetic.z - cal.oz) * cal.sz;
  float my =  (magEvent.magnetic.y - cal.oy) * cal.sy;
  float mz = -(magEvent.magnetic.x - cal.ox) * cal.sx;

  float pitch = atan2(-ax, sqrt(ay * ay + az * az));
  float roll  = atan2(ay, az);

  float Xh = mx * cos(pitch) + mz * sin(pitch);
  float Yh = mx * sin(roll) * sin(pitch) + my * cos(roll) - mz * sin(roll) * cos(pitch);

  float heading = atan2(Yh, Xh);

  float declinationAngle = -0.011;
  heading += declinationAngle;

  if (heading < 0) heading += 2 * PI;
  if (heading > 2 * PI) heading -= 2 * PI;

  return heading * 180.0 / PI;
}
void setupAccelerometer(void) {
  if (!accel.begin(0x18)) {   // SA0=GND -> 0x18 (default libreria es 0x19)
    Serial.println("Ooops, no LSM303 Accel detected ... Check your wiring!");
    while(1) { delay(100); }
  }
}

// ---------------------------------------------------------------------------
// Perfil de inmersión simulada: ~30 m
//
// Keyframes de un buceo recreativo real:
//   0 s   →   0 m  superficie
// 180 s   →  30 m  bajada 10 m/min (3 min)
// 1080 s  →  30 m  fondo 15 min (NDL Bühlmann ≈ 17 min a 30 m, GF 0.85)
// 1260 s  →   5 m  ascenso 8 m/min (~3 min)
// 1440 s  →   5 m  parada de seguridad 3 min
// 1500 s  →   0 m  superficie
// 3300 s  →   0 m  intervalo superficie 30 min (ciclo)
// ---------------------------------------------------------------------------
static float diveProfile(uint32_t t_s) {
  static const struct { uint32_t t; float d; } KF[] = {
    {    0,  0.0f},
    {  180, 30.0f},
    { 1080, 30.0f},
    { 1260,  5.0f},
    { 1440,  5.0f},
    { 1500,  0.0f},
    { 3300,  0.0f},
  };
  static const int N = (int)(sizeof(KF) / sizeof(KF[0]));

  t_s = t_s % 3300u;  // ciclo 55 min: 25 min inmersión + 30 min superficie

  if (t_s >= KF[N-1].t) return KF[N-1].d;
  for (int i = 1; i < N; i++) {
    if (t_s <= KF[i].t) {
      float alpha = (float)(t_s - KF[i-1].t) / (float)(KF[i].t - KF[i-1].t);
      return KF[i-1].d + alpha * (KF[i].d - KF[i-1].d);
    }
  }
  return 0.0f;
}

// ---------------------------------------------------------------------------
// MS5837 — inicialización en Wire1 (GPIO 8 SDA, GPIO 13 SCL)
// ---------------------------------------------------------------------------
static void setupPressureSensor(void) {
  Wire1.begin(MS5837_SDA_PIN, MS5837_SCL_PIN);
  s_ms5837Ok = ms5837_init();
  Serial.println(s_ms5837Ok ? "MS5837 OK" : "MS5837 no detectado — usando perfil simulado");
}

// ---------------------------------------------------------------------------
// Core 0 — adquisición de profundidad + Bühlmann ZHL-16C
// ---------------------------------------------------------------------------
void depthTask(void* /*param*/) {
  static DecoEngine engine;
  engine.init();  // tejidos inicializados a saturación de superficie

  const float DT_S = 0.1f;  // paso de integración: 100 ms

  for (;;) {
    float depth, tempC, bar = 0.0f;

    if (s_ms5837Ok) {
      ms5837_read(depth, tempC, bar);
    } else {
      uint32_t elapsed_s = (uint32_t)((millis() - g_diveStartMs) / 1000UL);
      depth = diveProfile(elapsed_s);
      tempC = 24.5f;
    }

    engine.update(depth, DT_S);
    DecoResult deco = engine.calculate(depth);

    if (xSemaphoreTake(g_dataMutex, portMAX_DELAY) == pdTRUE) {
      g_depth = depth;
      g_tempC = tempC;
      g_deco  = deco;
      xSemaphoreGive(g_dataMutex);
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void setup(void) {
  setupSerial();
  Serial.println("1. Serial OK");
  delay(500);

#if HAS_RGB_LED
  setupRGBLed();
  s_ledSampleMs = millis();
#endif

  setupTFT();
  Serial.println("2. TFT OK");
  delay(500);

  setupWire();
  Serial.println("3. Wire (I2C) OK");
  delay(500);

  setupPressureSensor();
  Serial.println("3b. MS5837 (Wire1) init");
  delay(200);

  setupMagnetometer();
  Serial.println("4. Magnetometer OK");
  delay(500);

  setupAccelerometer();
  Serial.println("5. Accelerometer OK");

  loadCalibration();
  setupSD();
  Serial.println("6. SD OK");

  setupWiFi();
  Serial.println("7. WiFi/HTTP OK");

  g_dataMutex = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(depthTask, "depthTask",
                          4096,     // stack bytes (ampliar cuando se añada deco)
                          nullptr,  // param
                          1,        // prioridad baja — no compite con WiFi stack
                          nullptr,  // handle (no necesario)
                          0);       // Core 0
  Serial.println("8. depthTask (Core 0) OK");

  g_diveStartMs = millis();
  Serial.println("Setup completado!");
}

static float s_compassLastHdg = -999.0f;

void loop(void) {
  dnsServer.processNextRequest();
  server.handleClient();

  sensors_event_t magEv, accEv;
  accel.getEvent(&accEv);
  mag.getEvent(&magEv);

  if (calState.collecting) {
    float mx = magEv.magnetic.x, my = magEv.magnetic.y, mz = magEv.magnetic.z;
    if (mx < calState.minX) calState.minX = mx;
    if (my < calState.minY) calState.minY = my;
    if (mz < calState.minZ) calState.minZ = mz;
    if (mx > calState.maxX) calState.maxX = mx;
    if (my > calState.maxY) calState.maxY = my;
    if (mz > calState.maxZ) calState.maxZ = mz;
    calState.samples++;
  }

  float heading = calculateHeading(magEv, accEv);
  g_heading = heading;

  // Leer profundidad, temperatura y datos de deco calculados por Core 0
  float depth, tempC;
  DecoResult deco;
  if (xSemaphoreTake(g_dataMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
    depth = g_depth;
    tempC = g_tempC;
    deco  = g_deco;
    xSemaphoreGive(g_dataMutex);
  } else {
    depth = g_depth;
    tempC = g_tempC;
    deco  = g_deco;
  }

  updateDiveSession(depth);

  unsigned long elapsed = s_inDive ? (millis() - s_sessStartMs) : 0;

  // Redibujado selectivo: brújula solo si heading cambió ≥1°
  bool hdgChanged = fabsf(heading - s_compassLastHdg) >= 1.0f;

  drawHeadingStrip(heading);
  drawDepth(depth);
  drawElapsed(elapsed);
  drawDiveData(tempC, deco);
  if (hdgChanged) {
    drawCompassBar(heading);
    s_compassLastHdg = heading;
  }

  logToSD(depth, tempC, heading);

#if HAS_RGB_LED
  updateRGBLed(depth);
#endif


  delay(30);
}
