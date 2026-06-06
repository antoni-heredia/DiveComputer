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
#include "deco.h"

// Pines ST7735
#define TFT_CS   11
#define TFT_RST  9
#define TFT_DC   10
#define TFT_MOSI 3
#define TFT_SCLK 4

// Pines microSD (comparte bus SPI con TFT)
#define SD_CS   6
#define SD_MISO 5

// Instanciamos la pantalla con hardware SPI (CS, DC, RST)
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

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

// Datos de buceo compartidos entre Core 0 (escribe) y Core 1 (lee/dibuja)
float          g_depth      = 0.0f;
float          g_tempC      = 24.5f;
unsigned long  g_diveStartMs = 0;
static SemaphoreHandle_t g_dataMutex = nullptr;
static DecoResult        g_deco      = {0.0f, 999.0f, false};

static bool          s_sdOk      = false;
static unsigned long s_lastLogMs = 0;

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
<div class="card">
 <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:8px">
  <span style="font-weight:bold">Registro SD <span id="sdcnt" style="font-size:.8rem;color:#8b949e">cargando...</span></span>
  <div>
   <button onclick="downloadChart()" style="font-size:.85rem;padding:6px 12px;border:0;border-radius:6px;background:#1f6feb;color:#fff;cursor:pointer;margin-right:4px">&#8659; PNG</button>
   <button class="reset" onclick="clearLog()" style="width:auto;padding:6px 12px">Borrar</button>
  </div>
 </div>
 <canvas id="chart" width="440" height="210" style="width:100%;border-radius:6px;display:block"></canvas>
</div>
<div class="card">
 <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:8px">
  <span style="font-weight:bold">Trayectoria <span style="font-size:.8rem;color:#8b949e">(velocidad constante)</span></span>
  <button onclick="downloadMap()" style="font-size:.85rem;padding:6px 12px;border:0;border-radius:6px;background:#1f6feb;color:#fff;cursor:pointer">&#8659; PNG</button>
 </div>
 <canvas id="mapcanvas" width="340" height="340" style="width:100%;border-radius:6px;display:block"></canvas>
</div>
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
function drawChart(data){
 var cv=document.getElementById('chart'),ctx=cv.getContext('2d');
 var W=cv.width,H=cv.height,pl=46,pr=46,pt=18,pb=28,cW=W-pl-pr,cH=H-pt-pb;
 ctx.fillStyle='#0e1116';ctx.fillRect(0,0,W,H);
 if(data.length<2){
  ctx.fillStyle='#8b949e';ctx.font='13px system-ui';ctx.textAlign='center';
  ctx.fillText('Sin datos suficientes',W/2,H/2);return;
 }
 var dep=data.map(function(r){return r[1]}),hdg=data.map(function(r){return r[3]});
 var dMin=Math.min.apply(null,dep),dMax=Math.max.apply(null,dep);
 var hMin=Math.min.apply(null,hdg),hMax=Math.max.apply(null,hdg);
 var dp=(dMax-dMin)*0.12||0.5;dMin-=dp;dMax+=dp;
 var hp=(hMax-hMin)*0.12||5;hMin-=hp;hMax+=hp;
 var tMin=data[0][0],tMax=data[data.length-1][0]||1;
 function tx(t){return pl+(t-tMin)/(tMax-tMin)*cW}
 function dy(d){return pt+cH-(d-dMin)/(dMax-dMin)*cH}
 function hy(h){return pt+cH-(h-hMin)/(hMax-hMin)*cH}
 // grid
 ctx.strokeStyle='#21262d';ctx.lineWidth=1;
 for(var i=0;i<=4;i++){
  var y=pt+i*cH/4;
  ctx.beginPath();ctx.moveTo(pl,y);ctx.lineTo(W-pr,y);ctx.stroke();
  ctx.fillStyle='#3fb950';ctx.font='10px system-ui';ctx.textAlign='right';
  ctx.fillText((dMax-i*(dMax-dMin)/4).toFixed(1),pl-4,y+3);
  ctx.fillStyle='#58a6ff';ctx.textAlign='left';
  ctx.fillText((hMax-i*(hMax-hMin)/4).toFixed(0),W-pr+4,y+3);
 }
 // axes
 ctx.strokeStyle='#30363d';ctx.lineWidth=1;
 ctx.beginPath();ctx.moveTo(pl,pt);ctx.lineTo(pl,pt+cH);ctx.lineTo(W-pr,pt+cH);ctx.lineTo(W-pr,pt);ctx.stroke();
 // x labels
 ctx.fillStyle='#8b949e';ctx.font='10px system-ui';ctx.textAlign='center';
 [0,0.25,0.5,0.75,1].forEach(function(f){
  var t=Math.round(tMin+f*(tMax-tMin));
  ctx.fillText(t+'s',pl+f*cW,pt+cH+14);
 });
 // depth line + fill
 ctx.strokeStyle='#3fb950';ctx.lineWidth=2;ctx.lineJoin='round';
 ctx.beginPath();
 data.forEach(function(r,i){if(i===0)ctx.moveTo(tx(r[0]),dy(r[1]));else ctx.lineTo(tx(r[0]),dy(r[1]))});
 ctx.stroke();
 ctx.globalAlpha=0.12;ctx.fillStyle='#3fb950';
 ctx.lineTo(tx(data[data.length-1][0]),pt+cH);ctx.lineTo(tx(data[0][0]),pt+cH);ctx.closePath();ctx.fill();
 ctx.globalAlpha=1;
 // heading line
 ctx.strokeStyle='#58a6ff';ctx.lineWidth=1.5;
 ctx.beginPath();
 data.forEach(function(r,i){if(i===0)ctx.moveTo(tx(r[0]),hy(r[3]));else ctx.lineTo(tx(r[0]),hy(r[3]))});
 ctx.stroke();
 // y labels
 ctx.save();ctx.translate(10,pt+cH/2);ctx.rotate(-Math.PI/2);
 ctx.fillStyle='#3fb950';ctx.font='bold 10px system-ui';ctx.textAlign='center';
 ctx.fillText('Prof (m)',0,0);ctx.restore();
 ctx.save();ctx.translate(W-8,pt+cH/2);ctx.rotate(Math.PI/2);
 ctx.fillStyle='#58a6ff';ctx.font='bold 10px system-ui';ctx.textAlign='center';
 ctx.fillText('Rumbo (°)',0,0);ctx.restore();
 // legend
 ctx.fillStyle='#3fb950';ctx.fillRect(pl+4,pt+2,10,3);
 ctx.fillStyle='#e6edf3';ctx.font='10px system-ui';ctx.textAlign='left';ctx.fillText('Prof',pl+18,pt+6);
 ctx.fillStyle='#58a6ff';ctx.fillRect(pl+52,pt+2,10,3);
 ctx.fillStyle='#e6edf3';ctx.fillText('Rumbo',pl+66,pt+6);
}
function downloadChart(){
 var cv=document.getElementById('chart');
 var url=cv.toDataURL('image/png');
 // Intento estándar (funciona en desktop y Android Chrome)
 var a=document.createElement('a');a.href=url;a.download='registro_buceo.png';
 document.body.appendChild(a);a.click();document.body.removeChild(a);
 // Overlay universal: en iOS el atributo download se ignora; pulsa largo sobre la imagen para guardar
 var ov=document.createElement('div');
 ov.style.cssText='position:fixed;top:0;left:0;right:0;bottom:0;background:rgba(0,0,0,.88);z-index:999;display:flex;flex-direction:column;align-items:center;justify-content:center;padding:16px;box-sizing:border-box';
 ov.innerHTML='<p style="color:#e6edf3;margin:0 0 10px;font-size:.85rem;text-align:center">En iOS: mant&eacute;n pulsado &rarr; Guardar imagen<br>En Android/PC: la descarga ya empez&oacute;</p>'
  +'<img src="'+url+'" style="max-width:100%;border-radius:8px;box-shadow:0 4px 24px #000">'
  +'<button onclick="this.parentElement.remove()" style="margin-top:14px;padding:10px 28px;border:0;border-radius:8px;background:#238636;color:#fff;font-size:1rem;cursor:pointer">Cerrar</button>';
 document.body.appendChild(ov);
}
function drawMap(data){
 var cv=document.getElementById('mapcanvas'),ctx=cv.getContext('2d');
 var W=cv.width,H=cv.height,pad=36;
 ctx.fillStyle='#0e1116';ctx.fillRect(0,0,W,H);
 if(data.length<2){
  ctx.fillStyle='#8b949e';ctx.font='13px system-ui';ctx.textAlign='center';
  ctx.fillText('Sin datos suficientes',W/2,H/2);return;
 }
 // Integrar trayectoria (velocidad=1 u/s, N arriba)
 var pts=[{x:0,y:0}];
 for(var i=1;i<data.length;i++){
  var dt=data[i][0]-data[i-1][0];
  var r=data[i][3]*Math.PI/180;
  var p=pts[pts.length-1];
  pts.push({x:p.x+Math.sin(r)*dt, y:p.y-Math.cos(r)*dt});
 }
 // Bounding box cuadrada con margen
 var xs=pts.map(function(p){return p.x}),ys=pts.map(function(p){return p.y});
 var x0=Math.min.apply(null,xs),x1=Math.max.apply(null,xs);
 var y0=Math.min.apply(null,ys),y1=Math.max.apply(null,ys);
 var span=Math.max(x1-x0,y1-y0)||10;
 var cx=(x0+x1)/2,cy=(y0+y1)/2;
 x0=cx-span*0.6;x1=cx+span*0.6;y0=cy-span*0.6;y1=cy+span*0.6;
 var sc=Math.min((W-2*pad)/(x1-x0),(H-2*pad)/(y1-y0));
 function mx(x){return W/2+(x-cx)*sc}
 function my(y){return H/2+(y-cy)*sc}
 // Grid
 ctx.strokeStyle='#1c2128';ctx.lineWidth=1;
 var step=Math.pow(10,Math.floor(Math.log10(span*0.4)));
 for(var g=Math.ceil(x0/step)*step;g<=x1;g+=step){
  ctx.beginPath();ctx.moveTo(mx(g),pad);ctx.lineTo(mx(g),H-pad);ctx.stroke();
 }
 for(var g=Math.ceil(y0/step)*step;g<=y1;g+=step){
  ctx.beginPath();ctx.moveTo(pad,my(g));ctx.lineTo(W-pad,my(g));ctx.stroke();
 }
 // Ejes cruzados en origen
 ctx.strokeStyle='#30363d';ctx.lineWidth=1;
 ctx.beginPath();ctx.moveTo(mx(0),pad);ctx.lineTo(mx(0),H-pad);ctx.stroke();
 ctx.beginPath();ctx.moveTo(pad,my(0));ctx.lineTo(W-pad,my(0));ctx.stroke();
 // Trayectoria con degradado azul→verde
 for(var i=1;i<pts.length;i++){
  var t=i/(pts.length-1);
  var r=Math.round(63*t+30),g2=Math.round(180*t+50),b=Math.round(150*(1-t)+30);
  ctx.strokeStyle='rgba('+r+','+g2+','+b+',0.85)';
  ctx.lineWidth=2.5;ctx.lineJoin='round';
  ctx.beginPath();ctx.moveTo(mx(pts[i-1].x),my(pts[i-1].y));ctx.lineTo(mx(pts[i].x),my(pts[i].y));ctx.stroke();
 }
 // Punto de inicio
 ctx.fillStyle='#ffffff';
 ctx.beginPath();ctx.arc(mx(pts[0].x),my(pts[0].y),5,0,Math.PI*2);ctx.fill();
 ctx.fillStyle='#8b949e';ctx.font='10px system-ui';ctx.textAlign='left';
 ctx.fillText('inicio',mx(pts[0].x)+7,my(pts[0].y)+4);
 // Flecha de posición actual (triángulo apuntando al rumbo)
 var last=pts[pts.length-1];
 var hdgR=data[data.length-1][3]*Math.PI/180;
 var lx=mx(last.x),ly=my(last.y),ar=13;
 ctx.fillStyle='#3fb950';
 ctx.beginPath();
 ctx.moveTo(lx+Math.sin(hdgR)*ar,   ly-Math.cos(hdgR)*ar);
 ctx.lineTo(lx+Math.sin(hdgR+2.3)*ar*0.4, ly-Math.cos(hdgR+2.3)*ar*0.4);
 ctx.lineTo(lx+Math.sin(hdgR-2.3)*ar*0.4, ly-Math.cos(hdgR-2.3)*ar*0.4);
 ctx.closePath();ctx.fill();
 // Rosa de norte (esquina superior derecha)
 var nx=W-22,ny=22,nl=12;
 ctx.strokeStyle='#ff4444';ctx.lineWidth=2;
 ctx.beginPath();ctx.moveTo(nx,ny+nl);ctx.lineTo(nx,ny-nl);ctx.stroke();
 ctx.fillStyle='#ff4444';
 ctx.beginPath();ctx.moveTo(nx,ny-nl-4);ctx.lineTo(nx-4,ny-nl+4);ctx.lineTo(nx+4,ny-nl+4);ctx.closePath();ctx.fill();
 ctx.fillStyle='#8b949e';ctx.font='bold 10px system-ui';ctx.textAlign='center';
 ctx.fillText('N',nx,ny+nl+12);
 // Escala (en unidades relativas, 1 u = 1 s·v)
 var barU=step,barPx=barU*sc;
 if(barPx>20&&barPx<W*0.4){
  var bx=pad+4,by=H-10;
  ctx.strokeStyle='#8b949e';ctx.lineWidth=2;
  ctx.beginPath();ctx.moveTo(bx,by);ctx.lineTo(bx+barPx,by);ctx.stroke();
  ctx.fillStyle='#8b949e';ctx.font='9px system-ui';ctx.textAlign='left';
  ctx.fillText(barU+' u',bx,by-3);
 }
}
function downloadMap(){
 var cv=document.getElementById('mapcanvas');
 var url=cv.toDataURL('image/png');
 var a=document.createElement('a');a.href=url;a.download='trayectoria.png';
 document.body.appendChild(a);a.click();document.body.removeChild(a);
 var ov=document.createElement('div');
 ov.style.cssText='position:fixed;top:0;left:0;right:0;bottom:0;background:rgba(0,0,0,.88);z-index:999;display:flex;flex-direction:column;align-items:center;justify-content:center;padding:16px;box-sizing:border-box';
 ov.innerHTML='<p style="color:#e6edf3;margin:0 0 10px;font-size:.85rem;text-align:center">En iOS: mant&eacute;n pulsado &rarr; Guardar imagen<br>En Android/PC: la descarga ya empez&oacute;</p>'
  +'<img src="'+url+'" style="max-width:100%;border-radius:8px;box-shadow:0 4px 24px #000">'
  +'<button onclick="this.parentElement.remove()" style="margin-top:14px;padding:10px 28px;border:0;border-radius:8px;background:#238636;color:#fff;font-size:1rem;cursor:pointer">Cerrar</button>';
 document.body.appendChild(ov);
}
function loadLog(){fetch('/data').then(function(r){
 if(!r.ok){document.getElementById('sdcnt').textContent='SD no disponible';return Promise.reject()}
 return r.text()
}).then(function(txt){
 var lines=txt.trim().split('\n');
 var data=lines.slice(1).filter(function(l){return l.trim().length>0}).map(function(l){
  var c=l.split(',');return[+c[0],+c[1],+c[2],+c[3]]
 });
 document.getElementById('sdcnt').textContent='('+data.length+' registros)';
 drawChart(data);
 drawMap(data);
}).catch(function(){})}
function clearLog(){if(confirm('Borrar el registro de la SD?'))fetch('/clearlog').then(function(){setTimeout(loadLog,200)})}
setInterval(loadLog,6000);loadLog();
</script></body></html>
)rawliteral";

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
    File f = SD.open("/dive.csv", FILE_WRITE);
    if (f) { f.println("t_s,prof_m,temp_c,rumbo_deg"); f.close(); }
  }
  server.send(200, "application/json", "{\"ok\":true}");
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
  server.on("/",        handleRoot);
  server.on("/start",   handleStart);
  server.on("/stop",    handleStop);
  server.on("/reset",   handleReset);
  server.on("/status",  handleStatus);
  server.on("/data",    handleData);
  server.on("/clearlog",handleClearLog);

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
static int   s_lastHdgInt   = -1;
static int   s_lastDepthInt = -1;
static int   s_lastTempInt  = -1;
static int   s_lastNdlInt   = -9999;   // NDL en segundos enteros
static int   s_lastCeilInt  = -1;      // techo en décimas de metro

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
  if (!SD.exists("/dive.csv")) {
    File f = SD.open("/dive.csv", FILE_WRITE);
    if (f) { f.println("t_s,prof_m,temp_c,rumbo_deg"); f.close(); }
  }
  Serial.println("SD OK");
}

void logToSD(float depth, float tempC, float heading, unsigned long elapsedMs) {
  if (!s_sdOk) return;
  if (millis() - s_lastLogMs < 5000UL) return;
  s_lastLogMs = millis();
  File f = SD.open("/dive.csv", FILE_APPEND);
  if (!f) return;
  char line[48];
  snprintf(line, sizeof(line), "%lu,%.1f,%.1f,%.1f", elapsedMs / 1000UL, depth, tempC, heading);
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
// Core 0 — adquisición de profundidad + Bühlmann ZHL-16C
// ---------------------------------------------------------------------------
void depthTask(void* /*param*/) {
  static DecoEngine engine;
  engine.init();  // tejidos inicializados a saturación de superficie

  const float DT_S = 0.1f;  // paso de integración: 100 ms

  for (;;) {
    // TODO: reemplazar con lectura real del sensor de presión (ej. MS5837 en Wire1)
    uint32_t elapsed_s = (uint32_t)((millis() - g_diveStartMs) / 1000UL);
    float depth = diveProfile(elapsed_s);

    engine.update(depth, DT_S);
    DecoResult deco = engine.calculate(depth);

    if (xSemaphoreTake(g_dataMutex, portMAX_DELAY) == pdTRUE) {
      g_depth = depth;
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

  unsigned long elapsed = millis() - g_diveStartMs;

  // Redibujado selectivo: brújula solo si heading cambió ≥1°
  bool hdgChanged = fabsf(heading - s_compassLastHdg) >= 1.0f;

  drawHeadingStrip(heading);
  drawDepth(depth);
  drawDiveData(tempC, deco);
  if (hdgChanged) {
    drawCompassBar(heading);
    s_compassLastHdg = heading;
  }

  logToSD(depth, tempC, heading, elapsed);

  Serial.printf("Heading: %.1f  Depth: %.1f m  NDL: %.0f min  Ceil: %.1f m\n",
                heading, depth, deco.ndl_min, deco.ceiling_m);

  delay(30);
}
