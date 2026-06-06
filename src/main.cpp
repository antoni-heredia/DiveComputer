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

// Pines ST7789
#define TFT_CS   11
#define TFT_RST  9
#define TFT_DC   10
#define TFT_MOSI 3
#define TFT_SCLK 4

// Instanciamos la pantalla (CS, DC, RST, MOSI, SCLK)
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

// Magnetómetro
Adafruit_HMC5883_Unified mag = Adafruit_HMC5883_Unified(12345);

// Acelerometro
Adafruit_LSM303_Accel_Unified accel = Adafruit_LSM303_Accel_Unified(54321);

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

Preferences prefs;
WebServer   server(80);

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

// Página del portal de calibración (servida desde flash)
const char PAGE_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="es"><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Brujula - Calibracion</title>
<style>
 body{font-family:system-ui,Arial,sans-serif;margin:0;background:#0e1116;color:#e6edf3}
 .wrap{max-width:480px;margin:0 auto;padding:18px}
 h1{font-size:1.25rem}
 .card{background:#161b22;border:1px solid #30363d;border-radius:10px;padding:16px;margin:12px 0}
 button{font-size:1rem;padding:12px;border:0;border-radius:8px;margin:5px 0;width:100%;cursor:pointer;color:#fff}
 .start{background:#238636}.stop{background:#1f6feb}.reset{background:#6e7681}
 .state{font-weight:bold}.calibrating{color:#f0883e}.idle{color:#3fb950}
 table{width:100%;border-collapse:collapse;font-size:.85rem}
 td{padding:3px 6px;border-bottom:1px solid #21262d;text-align:right}
 td:first-child{text-align:left;color:#8b949e}
 .big{font-size:2.2rem;text-align:center;margin:6px 0}
 .hint{font-size:.8rem;color:#8b949e}
</style></head><body><div class="wrap">
<h1>Calibracion de la brujula</h1>
<div class="card">
 <div class="big" id="heading">--</div>
 <div>Estado: <span class="state" id="state">--</span> &middot; Muestras: <span id="samples">0</span></div>
</div>
<div class="card">
 <button class="start" onclick="cmd('start')">Iniciar calibracion</button>
 <button class="stop" onclick="cmd('stop')">Detener y guardar</button>
 <button class="reset" onclick="if(confirm('Borrar calibracion?'))cmd('reset')">Resetear</button>
 <p class="hint">Pulsa Iniciar y gira el dispositivo lentamente en todas las orientaciones
 (dibuja un "8" en el aire) durante 20-30 s. Despues pulsa Detener y guardar.</p>
</div>
<div class="card"><table>
 <tr><td></td><td>X</td><td>Y</td><td>Z</td></tr>
 <tr><td>min</td><td id="mnx">-</td><td id="mny">-</td><td id="mnz">-</td></tr>
 <tr><td>max</td><td id="mxx">-</td><td id="mxy">-</td><td id="mxz">-</td></tr>
 <tr><td>offset</td><td id="ox">-</td><td id="oy">-</td><td id="oz">-</td></tr>
 <tr><td>escala</td><td id="sx">-</td><td id="sy">-</td><td id="sz">-</td></tr>
</table></div>
</div>
<script>
function cmd(c){fetch('/'+c).then(function(){setTimeout(upd,120)})}
function upd(){fetch('/status').then(function(r){return r.json()}).then(function(d){
 document.getElementById('heading').textContent=d.heading.toFixed(0)+'°';
 var s=document.getElementById('state');s.textContent=d.state;s.className='state '+d.state;
 document.getElementById('samples').textContent=d.samples;
 var mn=['mnx','mny','mnz'],mx=['mxx','mxy','mxz'];
 d.min.forEach(function(v,i){document.getElementById(mn[i]).textContent=v.toFixed(1)});
 d.max.forEach(function(v,i){document.getElementById(mx[i]).textContent=v.toFixed(1)});
 ['ox','oy','oz'].forEach(function(k,i){document.getElementById(k).textContent=d.offset[i].toFixed(2)});
 ['sx','sy','sz'].forEach(function(k,i){document.getElementById(k).textContent=d.scale[i].toFixed(3)});
})}
setInterval(upd,400);upd();
</script></body></html>
)rawliteral";

void handleRoot(void)  { server.send_P(200, "text/html", PAGE_HTML); }

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
  char buf[384];
  snprintf(buf, sizeof(buf),
    "{\"state\":\"%s\",\"samples\":%ld,"
    "\"min\":[%.1f,%.1f,%.1f],\"max\":[%.1f,%.1f,%.1f],"
    "\"offset\":[%.2f,%.2f,%.2f],\"scale\":[%.3f,%.3f,%.3f],"
    "\"heading\":%.1f}",
    calState.collecting ? "calibrating" : "idle", calState.samples,
    calState.minX, calState.minY, calState.minZ,
    calState.maxX, calState.maxY, calState.maxZ,
    cal.ox, cal.oy, cal.oz, cal.sx, cal.sy, cal.sz, g_heading);
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
  server.on("/",       handleRoot);
  server.on("/start",  handleStart);
  server.on("/stop",   handleStop);
  server.on("/reset",  handleReset);
  server.on("/status", handleStatus);
  server.begin();
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

// Datos de buceo — sustituir por lecturas de sensor real cuando esté disponible
float          g_depth    = 0.0f;   // metros
float          g_tempC    = 24.5f;  // grados C (placeholder)
unsigned long  g_diveStartMs = 0;   // millis() al inicio de la inmersión

// Estado de último renderizado para redibujado selectivo
static int   s_lastHdgInt   = -1;
static int   s_lastDepthInt = -1;
static int   s_lastTempInt  = -1;
static unsigned long s_lastTimeSec = 0xFFFFFFFF;

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

// Dibuja tiempo de inmersión y temperatura — solo si cambiaron
static void drawDiveData(float tempC, unsigned long elapsedMs) {
  unsigned long totalSec = elapsedMs / 1000UL;
  unsigned long mins = totalSec / 60;
  unsigned long secs = totalSec % 60;

  if (totalSec != s_lastTimeSec) {
    s_lastTimeSec = totalSec;
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
    char tbuf[12];
    snprintf(tbuf, sizeof(tbuf), "T %02lu:%02lu", mins, secs);
    tft.setCursor(4, Y_DATA_TOP + 5);
    tft.print(tbuf);
  }

  int tInt = (int)(tempC * 10.0f);
  if (tInt != s_lastTempInt) {
    s_lastTempInt = tInt;
    tft.setTextSize(1);
    tft.setTextColor(COL_ORG, ST77XX_BLACK);
    char tbuf[12];
    snprintf(tbuf, sizeof(tbuf), "%5.1f%cC", tempC, char(247));
    // 7 chars × 6 px = 42 px → alinear a la derecha
    tft.setCursor(SCR_W - 7 * 6 - 4, Y_DATA_TOP + 5);
    tft.print(tbuf);
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

static void drawAll(float heading, float depth, float tempC, unsigned long elapsedMs) {
  drawHeadingStrip(heading);
  drawDepth(depth);
  drawDiveData(tempC, elapsedMs);
  drawCompassBar(heading);  // siempre redibuja cuando heading cambia (ve su propia lógica abajo)
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

void setupTFT(void) {
  SPI.begin(TFT_SCLK, -1, TFT_MOSI);
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

  setupAccelerometer();
  Serial.println("5. Accelerometer OK");

  loadCalibration();
  setupWiFi();
  Serial.println("6. WiFi/HTTP OK");

  g_diveStartMs = millis();
  Serial.println("Setup completado!");
}

static float s_compassLastHdg = -999.0f;

void loop(void) {
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

  // Profundidad simulada — reemplazar con lectura de sensor de presión real
  g_depth = 5.0f + 4.5f * sinf((float)millis() / 15000.0f);
  if (g_depth < 0.0f) g_depth = 0.0f;

  // Temperatura placeholder — reemplazar con sensor real (ej. NTC o DS18B20)
  // g_tempC = readTemperatureSensor();

  unsigned long elapsed = millis() - g_diveStartMs;

  // Redibujado selectivo: brújula solo si heading cambió ≥1°
  bool hdgChanged = fabsf(heading - s_compassLastHdg) >= 1.0f;

  drawHeadingStrip(heading);
  drawDepth(g_depth);
  drawDiveData(g_tempC, elapsed);
  if (hdgChanged) {
    drawCompassBar(heading);
    s_compassLastHdg = heading;
  }

  Serial.printf("Heading: %.1f  Depth: %.1f m  T: %lu s\n",
                heading, g_depth, elapsed / 1000UL);

  delay(30);
}
