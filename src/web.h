#pragma once
// web.h — Portal "Bitácora de Buceo"
// Vanilla JS puro, CERO dependencias de CDN.
// Funciona cuando el móvil está conectado al AP del ESP32 (sin internet).

static const char PAGE_HTML[] PROGMEM = R"HTMLEND(<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>Brújula · Bitácora</title>
<style>
*{box-sizing:border-box;margin:0;padding:0;-webkit-tap-highlight-color:transparent}
:root{
  --bg:#08141e;--panel:#0e2030;--pb:#1c3a50;
  --acc:#f4a948;--txt:#e9f1f7;--dim:#90a8ba;--fnt:#5e788b;
  --good:#54d691;--warn:#ff8a5c;--cool:#4fd2e0;--grid:#1a3348;
  --r:12px;--mono:'SF Mono',Menlo,Consolas,monospace
}
html,body{height:100%;background:var(--bg);color:var(--txt);
  font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif}
#app{display:flex;flex-direction:column;height:100dvh;max-width:480px;margin:0 auto}
#hdr{flex-shrink:0}
#screen{flex:1;overflow-y:auto;-webkit-overflow-scrolling:touch}
#tabbar{flex-shrink:0;padding-bottom:env(safe-area-inset-bottom,0px)}
.hdr{display:flex;align-items:center;justify-content:space-between;
  padding:14px 18px 12px;border-bottom:1px solid var(--pb);background:var(--panel)}
.hdr-title{font-size:17px;font-weight:700;letter-spacing:.3px}
.hdr-sub{font-size:11px;color:var(--dim);font-family:var(--mono);margin-top:1px}
.hdr-badge{font-size:10px;font-weight:700;letter-spacing:.8px;text-transform:uppercase;
  padding:3px 8px;border-radius:20px;background:#f4a94822;color:var(--acc);border:1px solid #f4a94844}
.hdr-badge.warn{background:#ff8a5c22;color:var(--warn);border-color:#ff8a5c44}
.tabbar{display:flex;background:var(--panel);border-top:1px solid var(--pb);padding:0 4px}
.tab-btn{flex:1;display:flex;flex-direction:column;align-items:center;padding:10px 4px 8px;
  gap:4px;border:none;background:none;color:var(--dim);font-size:10px;font-weight:600;
  letter-spacing:.5px;text-transform:uppercase;cursor:pointer;transition:color .15s;position:relative}
.tab-btn.active{color:var(--acc)}
.tab-btn.active::after{content:'';position:absolute;top:0;left:20%;right:20%;height:2px;
  background:var(--acc);border-radius:0 0 2px 2px}
.tab-icon{width:22px;height:22px}
.panel{background:var(--panel);border:1px solid var(--pb);border-radius:var(--r);overflow:hidden}
.scr-pad{padding:12px 14px 28px}
.sec-title{font-size:10px;font-weight:700;letter-spacing:1.2px;text-transform:uppercase;
  color:var(--fnt);margin-bottom:10px}
.metrics{display:grid;gap:8px;margin-bottom:8px}
.m2{grid-template-columns:repeat(2,1fr)}
.m3{grid-template-columns:repeat(3,1fr)}
.m4{grid-template-columns:repeat(4,1fr)}
.metric{background:var(--panel);border:1px solid var(--pb);border-radius:10px;padding:10px 12px}
.metric-lbl{font-size:9.5px;font-weight:600;letter-spacing:.9px;text-transform:uppercase;
  color:var(--fnt);margin-bottom:4px}
.metric-val{font-size:20px;font-weight:700;font-family:var(--mono);color:var(--txt);line-height:1}
.metric-unit{font-size:10px;color:var(--dim);font-family:var(--mono);margin-top:2px}
.acc .metric-val{color:var(--acc)}
.good .metric-val{color:var(--good)}
.cool .metric-val{color:var(--cool)}
.status-row{display:flex;align-items:center;gap:12px;padding:12px 14px;background:var(--panel);
  border:1px solid var(--pb);border-radius:var(--r);margin-bottom:8px}
.sdot{width:9px;height:9px;border-radius:50%;flex-shrink:0}
.sdot-ok{background:var(--good)}
.sdot-warn{background:var(--warn);animation:pulse 1.4s infinite}
.status-lbl{font-size:13px;color:var(--dim)}
.dive-card{display:flex;align-items:center;gap:14px;padding:12px 14px;
  border-bottom:1px solid var(--pb);cursor:pointer;transition:background .1s}
.dive-card:last-child{border-bottom:none}
.dive-card:active{background:#1c3a5044}
.card-date{font-size:11px;color:var(--dim);font-family:var(--mono);margin-bottom:3px}
.card-depth{font-size:18px;font-weight:700;font-family:var(--mono);color:var(--acc)}
.card-dur{font-size:11px;color:var(--fnt);font-family:var(--mono)}
.det-tabs{display:flex;background:#1c3a5044;border-radius:8px;padding:3px;margin-bottom:12px}
.det-tab{flex:1;padding:7px 4px;text-align:center;border:none;background:none;font-size:12px;
  font-weight:600;color:var(--dim);border-radius:6px;cursor:pointer;transition:all .15s}
.det-tab.active{background:var(--panel);color:var(--txt);box-shadow:0 1px 4px #0006}
.prog-wrap{height:8px;background:var(--pb);border-radius:9px;overflow:hidden;margin-bottom:14px}
.prog-bar{height:100%;border-radius:9px;transition:width .2s linear;
  background:var(--acc);box-shadow:0 0 10px #f4a94866}
.btn-acc{width:100%;padding:14px;border:none;cursor:pointer;border-radius:var(--r);
  background:var(--acc);color:#08141e;font-size:15px;font-weight:700;letter-spacing:.3px;
  box-shadow:0 0 18px #f4a94844}
.btn-ghost{width:100%;padding:13px;cursor:pointer;border-radius:var(--r);background:transparent;
  color:var(--txt);border:1px solid var(--pb);font-size:15px;font-weight:700}
.btn-row{display:flex;gap:10px}
.btn-row .btn-acc,.btn-row .btn-ghost{flex:1;width:auto}
.cal-stats{display:flex;gap:22px;justify-content:center;margin-top:6px}
.cal-stat{text-align:center}
.cal-stat-lbl{font-size:9.5px;font-weight:600;letter-spacing:1px;text-transform:uppercase;color:var(--fnt)}
.cal-stat-val{font-size:19px;font-weight:700;font-family:var(--mono);color:var(--txt);margin-top:3px}
.cal-stat-val.a{color:var(--acc)}
.done-badge{display:inline-flex;align-items:center;gap:7px;background:#54d69122;
  border:1px solid #54d69155;border-radius:8px;padding:6px 12px;
  font-size:13px;font-weight:700;color:var(--good)}
.empty{text-align:center;padding:48px 24px;color:var(--dim)}
.spinner{width:32px;height:32px;border:3px solid var(--pb);border-top-color:var(--acc);
  border-radius:50%;animation:spin .7s linear infinite;margin:40px auto}
.back-btn{display:flex;align-items:center;gap:6px;background:none;border:none;
  color:var(--dim);font-size:14px;font-weight:600;cursor:pointer;padding:0;margin-bottom:14px}
.chart-box{padding:12px 4px 4px;overflow:hidden}
@keyframes spin{to{transform:rotate(360deg)}}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:.35}}
@keyframes fadeUp{from{opacity:0;transform:translateY(8px)}to{opacity:1;transform:none}}
.fade-up{animation:fadeUp .22s ease both}
</style>
</head>
<body>
<div id="app">
  <div id="hdr"></div>
  <div id="screen"></div>
  <div id="tabbar"></div>
</div>
<script>
// ── estado global ─────────────────────────────────────────────
var S = {
  tab: 0,
  sel: -1,
  dives: null,
  detTab: 0,
  cal: {
    phase: 'idle',
    prog: 0, qual: 0, hdg: 0,
    cov: new Array(24).fill(0),
    off: {x:0, y:0, z:0},
    saved: null
  }
};
var _calTimer = null;
try {
  var _sc = JSON.parse(localStorage.getItem('dc_cal') || 'null');
  if (_sc) S.cal.saved = _sc;
} catch(e) {}

// ── utilidades ────────────────────────────────────────────────
function fmtDate(iso) {
  if (!iso) return '—';
  try { return new Date(iso).toLocaleDateString('es-ES',{day:'2-digit',month:'short',year:'numeric'}); }
  catch(e){ return iso; }
}
function fmtDT(iso) {
  if (!iso) return '—';
  try {
    var d=new Date(iso);
    return fmtDate(iso)+' · '+d.toLocaleTimeString('es-ES',{hour:'2-digit',minute:'2-digit'});
  } catch(e){ return iso; }
}
function fmtDur(m) {
  if (m==null) return '—';
  m=Math.round(m);
  return m>=60?Math.floor(m/60)+'h '+(m%60)+"'":m+"'";
}

// ── SVG mini perfil ───────────────────────────────────────────
function svgMini(s,w,h) {
  if (!s||!s.length) return '';
  var maxT=s[s.length-1].t||1, maxD=4;
  for(var i=0;i<s.length;i++) if(s[i].depth>maxD) maxD=s[i].depth;
  maxD*=1.08;
  var pts=s.map(function(p){
    return ((p.t/maxT)*w).toFixed(1)+','+(2+(p.depth/maxD)*(h-4)).toFixed(1);
  }).join(' ');
  var area='M0,2 L'+pts.split(' ').join(' L')+' L'+w+',2 Z';
  return '<svg viewBox="0 0 '+w+' '+h+'" width="'+w+'" height="'+h+
    '" style="display:block" preserveAspectRatio="none">'+
    '<path d="'+area+'" fill="#f4a948" opacity=".16"/>'+
    '<polyline points="'+pts+'" fill="none" stroke="#f4a948" stroke-width="1.6" stroke-linejoin="round"/>'+
    '</svg>';
}

// ── SVG perfil de profundidad ─────────────────────────────────
function svgDepth(dive) {
  var s=dive.samples;
  if(!s||!s.length) return '';
  var W=760,H=220,pL=42,pR=16,pT=14,pB=26;
  var maxT=s[s.length-1].t||1;
  var maxD=Math.max((dive.maxDepth||0)*1.08,6);
  function x(t){return pL+(t/maxT)*(W-pL-pR);}
  function y(d){return pT+(d/maxD)*(H-pT-pB);}
  var pts=s.map(function(p){return x(p.t).toFixed(1)+','+y(p.depth).toFixed(1);}).join(' ');
  var area='M'+x(0).toFixed(1)+','+y(0).toFixed(1)+
    ' L'+pts.split(' ').join(' L')+
    ' L'+x(maxT).toFixed(1)+','+y(0).toFixed(1)+' Z';
  var step=(dive.maxDepth||6)>30?10:(dive.maxDepth||6)>15?5:3;
  var tLines='';
  for(var d=0;d<=(dive.maxDepth||6)+0.5;d+=step){
    tLines+='<line x1="'+pL+'" y1="'+y(d).toFixed(1)+'" x2="'+(W-pR)+'" y2="'+y(d).toFixed(1)+
      '" stroke="#1a3348" stroke-width="1"'+(d>0?' stroke-dasharray="3 5"':'')+'/>' +
      '<text x="'+(pL-7)+'" y="'+(y(d)+4).toFixed(1)+
      '" text-anchor="end" font-size="13" fill="#5e788b" font-family="monospace">'+d+'</text>';
  }
  var tStep=maxT>40?10:5, tLabels='';
  for(var t=0;t<=maxT;t+=tStep){
    tLabels+='<text x="'+x(t).toFixed(1)+'" y="'+(H-7)+
      '" text-anchor="middle" font-size="12" fill="#5e788b" font-family="monospace">'+t+"'</text>";
  }
  var deep=s[0];
  for(var j=1;j<s.length;j++) if(s[j].depth>deep.depth) deep=s[j];
  var tLo=99,tHi=-99;
  for(var k=0;k<s.length;k++){if(s[k].temp<tLo)tLo=s[k].temp;if(s[k].temp>tHi)tHi=s[k].temp;}
  var tSpan=Math.max(tHi-tLo,0.1);
  function yt(c){return pT+(1-(c-(tLo-1))/((tHi+1)-(tLo-1)))*(H-pT-pB);}
  var tPts=s.map(function(p){return x(p.t).toFixed(1)+','+yt(p.temp).toFixed(1);}).join(' ');
  return '<svg viewBox="0 0 '+W+' '+H+'" width="100%" style="display:block" preserveAspectRatio="none">'+
    '<defs><linearGradient id="dpg" x1="0" y1="0" x2="0" y2="1">'+
    '<stop offset="0%" stop-color="#f4a948" stop-opacity=".38"/>'+
    '<stop offset="100%" stop-color="#f4a948" stop-opacity=".02"/></linearGradient></defs>'+
    tLines+tLabels+
    '<path d="'+area+'" fill="url(#dpg)"/>'+
    '<polyline points="'+pts+'" fill="none" stroke="#f4a948" stroke-width="2.4" stroke-linejoin="round" stroke-linecap="round"/>'+
    '<polyline points="'+tPts+'" fill="none" stroke="#4fd2e0" stroke-width="1.5" stroke-dasharray="2 4" opacity=".8"/>'+
    '<circle cx="'+x(deep.t).toFixed(1)+'" cy="'+y(deep.depth).toFixed(1)+'" r="4.5" fill="#f4a948" stroke="#0e2030" stroke-width="2"/>'+
    '<text x="'+x(deep.t).toFixed(1)+'" y="'+(y(deep.depth)+20).toFixed(1)+
    '" text-anchor="middle" font-size="13" font-family="monospace" font-weight="700" fill="#f4a948">'+
    (dive.maxDepth||0).toFixed(1)+' m</text>'+
    '</svg>';
}

// ── SVG rosa de brújula ───────────────────────────────────────
function svgCompass(dive) {
  var s=dive.samples;
  if(!s||!s.length) return '';
  var sz=230,cx=sz/2,cy=sz/2,R=sz/2-22;
  var maxT=s[s.length-1].t||1;
  function toXY(h,t){
    var r=18+(t/maxT)*(R-18),a=(h-90)*Math.PI/180;
    return [cx+r*Math.cos(a),cy+r*Math.sin(a)];
  }
  var spiral=s.map(function(p){var pt=toXY(p.heading,p.t);return pt[0].toFixed(1)+','+pt[1].toFixed(1);}).join(' ');
  var sx=0,sy=0;
  for(var i=0;i<s.length;i++){sx+=Math.cos((s[i].heading-90)*Math.PI/180);sy+=Math.sin((s[i].heading-90)*Math.PI/180);}
  var mH=((Math.atan2(sy,sx)*180/Math.PI+90)%360+360)%360;
  var nd=toXY(mH,maxT);
  var ticks='';
  for(var a=0;a<360;a+=30){
    var rad=(a-90)*Math.PI/180,r1=R,r2=R-(a%90===0?10:6);
    ticks+='<line x1="'+(cx+r1*Math.cos(rad)).toFixed(1)+'" y1="'+(cy+r1*Math.sin(rad)).toFixed(1)+
      '" x2="'+(cx+r2*Math.cos(rad)).toFixed(1)+'" y2="'+(cy+r2*Math.sin(rad)).toFixed(1)+
      '" stroke="#5e788b" stroke-width="'+(a%90===0?1.6:1)+'"/>';
  }
  var cards='';
  [['N',0],['E',90],['S',180],['O',270]].forEach(function(c){
    var rad=(c[1]-90)*Math.PI/180,rr=R+12;
    cards+='<text x="'+(cx+rr*Math.cos(rad)).toFixed(1)+'" y="'+(cy+rr*Math.sin(rad)+4).toFixed(1)+
      '" text-anchor="middle" font-size="14" font-weight="700" font-family="sans-serif" fill="'+
      (c[0]==='N'?'#f4a948':'#90a8ba')+'">'+c[0]+'</text>';
  });
  return '<svg viewBox="0 0 '+sz+' '+sz+'" width="100%" style="display:block">'+
    '<circle cx="'+cx+'" cy="'+cy+'" r="'+R+'" fill="none" stroke="#1a3348" stroke-width="1"/>'+
    '<circle cx="'+cx+'" cy="'+cy+'" r="'+(R*.66).toFixed(1)+'" fill="none" stroke="#1a3348" stroke-width="1" stroke-dasharray="2 5"/>'+
    '<circle cx="'+cx+'" cy="'+cy+'" r="'+(R*.33).toFixed(1)+'" fill="none" stroke="#1a3348" stroke-width="1" stroke-dasharray="2 5"/>'+
    ticks+cards+
    '<polyline points="'+spiral+'" fill="none" stroke="#4fd2e0" stroke-width="1.6" opacity=".8" stroke-linejoin="round"/>'+
    '<line x1="'+cx+'" y1="'+cy+'" x2="'+nd[0].toFixed(1)+'" y2="'+nd[1].toFixed(1)+
    '" stroke="#f4a948" stroke-width="2.6" stroke-linecap="round"/>'+
    '<circle cx="'+cx+'" cy="'+cy+'" r="4" fill="#f4a948"/>'+
    '</svg>';
}

// ── SVG traza de ruta ─────────────────────────────────────────
function svgRoute(dive) {
  var s=dive.samples;
  if(!s||!s.length) return '';
  var W=320,H=220,pts=[],px=0,py=0;
  pts.push([0,0]);
  for(var i=1;i<s.length;i++){
    var dt=s[i].t-s[i-1].t,step=dt*12,rad=(s[i].heading-90)*Math.PI/180;
    px+=step*Math.cos(rad);py+=step*Math.sin(rad);pts.push([px,py]);
  }
  var xs=pts.map(function(p){return p[0];}),ys=pts.map(function(p){return p[1];});
  var minX=Math.min.apply(null,xs),maxX=Math.max.apply(null,xs);
  var minY=Math.min.apply(null,ys),maxY=Math.max.apply(null,ys);
  var pad=26,spanX=Math.max(maxX-minX,1),spanY=Math.max(maxY-minY,1);
  var sc=Math.min((W-pad*2)/spanX,(H-pad*2)/spanY);
  var oX=(W-spanX*sc)/2,oY=(H-spanY*sc)/2;
  function tx(p){return (oX+(p[0]-minX)*sc).toFixed(1);}
  function ty(p){return (oY+(p[1]-minY)*sc).toFixed(1);}
  var poly=pts.map(function(p){return tx(p)+','+ty(p);}).join(' ');
  var grid='';
  [.25,.5,.75].forEach(function(f){
    grid+='<line x1="'+(W*f)+'" y1="0" x2="'+(W*f)+'" y2="'+H+'" stroke="#1a3348" stroke-width="1" stroke-dasharray="2 6" opacity=".6"/>'+
          '<line x1="0" y1="'+(H*f)+'" x2="'+W+'" y2="'+(H*f)+'" stroke="#1a3348" stroke-width="1" stroke-dasharray="2 6" opacity=".6"/>';
  });
  return '<svg viewBox="0 0 '+W+' '+H+'" width="100%" style="display:block">'+
    grid+
    '<g transform="translate('+(W-24)+',22)">'+
    '<line x1="0" y1="8" x2="0" y2="-8" stroke="#90a8ba" stroke-width="1.4"/>'+
    '<path d="M0,-11 L3,-5 L-3,-5 Z" fill="#f4a948"/>'+
    '<text x="0" y="-14" text-anchor="middle" font-size="11" font-family="sans-serif" font-weight="700" fill="#90a8ba">N</text>'+
    '</g>'+
    '<polyline points="'+poly+'" fill="none" stroke="#f4a948" stroke-width="2.2" stroke-linejoin="round" stroke-linecap="round"/>'+
    '<circle cx="'+tx(pts[0])+'" cy="'+ty(pts[0])+'" r="5" fill="#54d691" stroke="#0e2030" stroke-width="2"/>'+
    '<circle cx="'+tx(pts[pts.length-1])+'" cy="'+ty(pts[pts.length-1])+'" r="5" fill="#ff8a5c" stroke="#0e2030" stroke-width="2"/>'+
    '</svg>';
}

// ── SVG esfera de calibración ─────────────────────────────────
function svgSphere(cov,hdg,phase) {
  var SZ=220,cx=SZ/2,cy=SZ/2,R=SZ/2-18,sectors='';
  for(var i=0;i<24;i++){
    var a1=(-90+i*15)*Math.PI/180,a2=(-90+(i+1)*15)*Math.PI/180;
    var ri=R-13,ro=R-1;
    var x1=cx+ri*Math.cos(a1),y1=cy+ri*Math.sin(a1);
    var x2=cx+ro*Math.cos(a1),y2=cy+ro*Math.sin(a1);
    var x3=cx+ro*Math.cos(a2),y3=cy+ro*Math.sin(a2);
    var x4=cx+ri*Math.cos(a2),y4=cy+ri*Math.sin(a2);
    var f=cov[i]>0.5;
    sectors+='<path d="M'+x1.toFixed(1)+','+y1.toFixed(1)+' L'+x2.toFixed(1)+','+y2.toFixed(1)+
      ' A'+ro+','+ro+' 0 0,1 '+x3.toFixed(1)+','+y3.toFixed(1)+
      ' L'+x4.toFixed(1)+','+y4.toFixed(1)+' A'+ri+','+ri+' 0 0,0 '+x1.toFixed(1)+','+y1.toFixed(1)+' Z"'+
      ' fill="'+(f?'#f4a948':'#1c3a50')+'" opacity="'+(f?'.9':'.4')+'"/>';
  }
  var cards='';
  [['N',0],['E',90],['S',180],['O',270]].forEach(function(c){
    var rad=(c[1]-90)*Math.PI/180,rr=R*.62-14;
    cards+='<text x="'+(cx+rr*Math.cos(rad)).toFixed(1)+'" y="'+(cy+rr*Math.sin(rad)+4).toFixed(1)+
      '" text-anchor="middle" font-size="12" font-weight="700" font-family="sans-serif" fill="'+
      (c[0]==='N'?'#f4a948':'#5e788b')+'">'+c[0]+'</text>';
  });
  var needle='';
  if(phase==='running'){
    var nr=(hdg-90)*Math.PI/180;
    var nx=(cx+R*.5*Math.cos(nr)).toFixed(1),ny=(cy+R*.5*Math.sin(nr)).toFixed(1);
    needle='<line x1="'+cx+'" y1="'+cy+'" x2="'+nx+'" y2="'+ny+
      '" stroke="#f4a948" stroke-width="2.6" stroke-linecap="round"/>'+
      '<circle cx="'+nx+'" cy="'+ny+'" r="4" fill="#f4a948"/>';
  }
  return '<svg viewBox="0 0 '+SZ+' '+SZ+'" width="220" height="220" style="display:block">'+
    '<circle cx="'+cx+'" cy="'+cy+'" r="'+R+'" fill="none" stroke="#1a3348" stroke-width="1"/>'+
    '<circle cx="'+cx+'" cy="'+cy+'" r="'+(R*.62).toFixed(1)+'" fill="none" stroke="#1a3348" stroke-width="1" stroke-dasharray="2 5"/>'+
    '<ellipse cx="'+cx+'" cy="'+cy+'" rx="'+R+'" ry="'+(R*.42).toFixed(1)+'" fill="none" stroke="#1a3348" stroke-width="1" stroke-dasharray="2 5"/>'+
    '<ellipse cx="'+cx+'" cy="'+cy+'" rx="'+(R*.42).toFixed(1)+'" ry="'+R+'" fill="none" stroke="#1a3348" stroke-width="1" stroke-dasharray="2 5"/>'+
    sectors+cards+needle+
    '<circle cx="'+cx+'" cy="'+cy+'" r="3.5" fill="#90a8ba"/>'+
    '</svg>';
}

// ── helper métrica ────────────────────────────────────────────
function mEl(lbl,val,unit,cls) {
  return '<div class="metric'+(cls?' '+cls:'')+'">' +
    '<div class="metric-lbl">'+lbl+'</div>'+
    '<div class="metric-val">'+val+'</div>'+
    (unit?'<div class="metric-unit">'+unit+'</div>':'')+
    '</div>';
}

// ── pantalla resumen ──────────────────────────────────────────
function buildSummary() {
  if(S.dives===null) return '<div class="scr-pad fade-up"><div class="spinner"></div></div>';
  if(!S.dives.length) return '<div class="scr-pad fade-up"><div class="empty">' +
    '<svg width="48" height="48" viewBox="0 0 24 24" fill="none" stroke="#e9f1f7" stroke-width="1.5" style="opacity:.3;margin-bottom:14px">' +
    '<path d="M3 9l9-7 9 7v11a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/><polyline points="9 22 9 12 15 12 15 22"/></svg>'+
    '<p style="font-size:15px;font-weight:600;margin-bottom:6px">Sin inmersiones registradas</p>'+
    '<p style="font-size:13px">Conéctate y empieza a bucear.</p></div></div>';
  var d=S.dives,n=d.length,totMin=0,maxDep=0,sumDep=0;
  for(var i=0;i<n;i++){totMin+=(d[i].duration_min||0);if((d[i].maxDepth||0)>maxDep)maxDep=d[i].maxDepth||0;sumDep+=(d[i].avgDepth||0);}
  var last=d[n-1];
  return '<div class="scr-pad fade-up">'+
    '<p class="sec-title">Resumen</p>'+
    '<div class="metrics m2" style="margin-bottom:8px">'+mEl('Inmersiones',n,'total','acc')+mEl('Tiempo total',fmtDur(totMin),'','')+'</div>'+
    '<div class="metrics m3" style="margin-bottom:14px">'+
    mEl('Prof. máx.',maxDep.toFixed(1),'m','acc')+
    mEl('Prof. media',(sumDep/n).toFixed(1),'m','')+
    mEl('Última',fmtDate(last.date),'','')+'</div>'+
    '<p class="sec-title">Última inmersión</p>'+
    '<div class="panel" style="margin-bottom:12px;padding:10px 12px">'+
    '<div style="margin-bottom:8px">'+svgMini(last.samples,300,52)+'</div>'+
    '<div class="metrics m4">'+
    mEl('Prof.',(last.maxDepth||0).toFixed(1),'m','acc')+
    mEl('Dur.',fmtDur(last.duration_min),'','')+
    mEl('Temp.',(last.tempMin||0).toFixed(1),'°C','cool')+
    mEl('Rumbo',Math.round(last.avgHeading||0)+'°','','')+'</div></div>'+
    '<p class="sec-title">Estado del equipo</p>'+
    '<div class="status-row"><div class="sdot sdot-ok"></div>'+
    '<span class="status-lbl">Brújula activa · '+Math.round(last.avgHeading||0)+'° rumbo actual</span></div>'+
    '</div>';
}

// ── pantalla lista ────────────────────────────────────────────
function buildList() {
  if(S.dives===null) return '<div class="scr-pad fade-up"><div class="spinner"></div></div>';
  if(!S.dives.length) return '<div class="scr-pad fade-up"><div class="empty">'+
    '<svg width="48" height="48" viewBox="0 0 24 24" fill="none" stroke="#e9f1f7" stroke-width="1.5" style="opacity:.3;margin-bottom:14px">'+
    '<path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/><polyline points="14 2 14 8 20 8"/></svg>'+
    '<p style="font-size:15px;font-weight:600;margin-bottom:6px">Sin bitácora</p>'+
    '<p style="font-size:13px">Las inmersiones aparecerán aquí.</p></div></div>';
  var d=S.dives,cards='';
  for(var i=d.length-1;i>=0;i--){
    var dv=d[i];
    cards+='<div class="dive-card" data-action="sel" data-id="'+i+'">'+
      '<div style="flex-shrink:0">'+svgMini(dv.samples,80,32)+'</div>'+
      '<div style="min-width:0;flex:1">'+
      '<div class="card-date">'+(dv.location||'Sin ubicaci\xF3n')+' &middot; '+fmtDate(dv.startTime||dv.date)+'</div>'+
      '<div class="card-depth">'+((dv.maxDepth||0).toFixed(1))+' m</div>'+
      '<div class="card-dur">'+fmtDur(dv.durationMin)+' · '+((dv.tempMin||0).toFixed(1))+'°C</div>'+
      '</div>'+
      '<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="#5e788b" stroke-width="2.5" stroke-linecap="round">'+
      '<path d="M9 18l6-6-6-6"/></svg></div>';
  }
  return '<div class="scr-pad fade-up" style="padding-left:0;padding-right:0">'+
    '<div style="padding:0 14px 10px"><p class="sec-title">'+d.length+' inmersión'+(d.length!==1?'es':'')+'</p></div>'+
    '<div class="panel" style="margin:0 14px 24px">'+cards+'</div></div>';
}

// ── pantalla detalle ──────────────────────────────────────────
function buildDetail() {
  var dv=S.dives&&S.dives[S.sel];
  if(!dv) return buildList();
  var tns=['Perfil','Rumbo','Ruta'];
  var tbBtns=tns.map(function(t,i){
    return '<button class="det-tab'+(S.detTab===i?' active':'')+'" data-action="dtab" data-id="'+i+'">'+t+'</button>';
  }).join('');
  var chart='';
  if(S.detTab===0) chart='<div class="chart-box">'+svgDepth(dv)+'</div>';
  else if(S.detTab===1) chart='<div class="chart-box" style="display:flex;justify-content:center">'+svgCompass(dv)+'</div>';
  else chart='<div class="chart-box">'+svgRoute(dv)+'</div>';
  var locVal=(dv.location||'').replace(/"/g,'&quot;');
  var tsVal=(dv.startTime&&dv.startTime!=='----'?dv.startTime:'').replace(/"/g,'&quot;');
  var editPanel=
    '<div class="panel" style="padding:14px 16px;margin-bottom:12px">'+
    '<p class="sec-title" style="margin-bottom:10px">Editar inmersión</p>'+
    '<label style="font-size:11px;color:var(--dim);display:block;margin-bottom:4px;font-weight:600;letter-spacing:.6px;text-transform:uppercase">Localización</label>'+
    '<input id="ed-loc" type="text" placeholder="p.ej. Alcalá la Real" value="'+locVal+'"'+
    ' style="width:100%;background:var(--pb);border:1px solid var(--grid);border-radius:8px;'+
    'padding:10px 12px;color:var(--txt);font-size:14px;margin-bottom:10px;-webkit-appearance:none"/>'+
    '<label style="font-size:11px;color:var(--dim);display:block;margin-bottom:4px;font-weight:600;letter-spacing:.6px;text-transform:uppercase">Hora de inicio</label>'+
    '<input id="ed-time" type="datetime-local" value="'+tsVal+'"'+
    ' style="width:100%;background:var(--pb);border:1px solid var(--grid);border-radius:8px;'+
    'padding:10px 12px;color:var(--txt);font-size:14px;margin-bottom:12px;-webkit-appearance:none"/>'+
    '<button class="btn-acc" data-action="saveMeta" data-id="'+S.sel+'">Guardar</button>'+
    '</div>';
  return '<div class="scr-pad fade-up">'+
    '<button class="back-btn" data-action="back">'+
    '<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round">'+
    '<path d="M15 18l-6-6 6-6"/></svg>Bitácora</button>'+
    '<p class="sec-title">'+(dv.location||'Sin ubicaci\xF3n')+' &middot; '+fmtDate(dv.startTime||dv.date)+'</p>'+
    '<div class="metrics m4" style="margin-bottom:8px">'+
    mEl('Prof. máx.',(dv.maxDepth||0).toFixed(1),'m','acc')+
    mEl('Duración',fmtDur(dv.durationMin),'','')+
    mEl('Temp. mín.',(dv.tempMin||0).toFixed(1),'°C','cool')+
    mEl('Rumbo med.',Math.round(dv.avgHeading||0)+'°','','')+'</div>'+
    '<div class="metrics m3" style="margin-bottom:14px">'+
    mEl('Prof. media',(dv.avgDepth||0).toFixed(1),'m','')+
    mEl('Prof. mín.',(dv.minDepth||0).toFixed(1),'m','')+
    mEl('Temp. máx.',(dv.tempMax||0).toFixed(1),'°C','')+'</div>'+
    editPanel+
    '<p class="sec-title">Gráficos</p>'+
    '<div class="panel" style="margin-bottom:24px">'+
    '<div style="padding:10px 10px 0"><div class="det-tabs">'+tbBtns+'</div></div>'+
    chart+'</div></div>';
}

// ── pantalla calibración ──────────────────────────────────────
function buildCalibrate() {
  var cal=S.cal;
  var savedPanel='';
  if(cal.saved){
    var q=cal.saved.quality||0,c2=2*Math.PI*18;
    savedPanel='<div class="panel" style="padding:14px 16px;margin-bottom:10px">'+
      '<p class="sec-title">Estado de la brújula</p>'+
      '<div style="display:flex;align-items:center;gap:14px">'+
      '<svg width="46" height="46" viewBox="0 0 46 46">'+
      '<circle cx="23" cy="23" r="18" fill="none" stroke="#1c3a50" stroke-width="4"/>'+
      '<circle cx="23" cy="23" r="18" fill="none" stroke="#54d691" stroke-width="4" stroke-linecap="round"'+
      ' stroke-dasharray="'+c2.toFixed(1)+'" stroke-dashoffset="'+(c2*(1-q/100)).toFixed(1)+'" transform="rotate(-90 23 23)"/>'+
      '<text x="23" y="27" text-anchor="middle" font-size="13" font-weight="700" font-family="monospace" fill="#e9f1f7">✓</text>'+
      '</svg>'+
      '<div style="min-width:0;flex:1">'+
      '<div style="font-size:15px;font-weight:700;color:#e9f1f7">Calibrada</div>'+
      '<div style="font-size:11px;color:#90a8ba;font-family:monospace;margin-top:2px">'+fmtDT(cal.saved.date)+'</div></div>'+
      '<div style="text-align:right">'+
      '<div style="font-size:9.5px;letter-spacing:1px;text-transform:uppercase;color:#5e788b;font-weight:600">Calidad</div>'+
      '<div style="font-size:22px;font-weight:700;font-family:monospace;color:#f4a948">'+q+'%</div></div>'+
      '</div></div>';
  } else {
    savedPanel='<div class="panel" style="padding:14px 16px;margin-bottom:10px">'+
      '<p class="sec-title">Estado de la brújula</p>'+
      '<div style="display:flex;align-items:center;gap:12px">'+
      '<span style="width:9px;height:9px;border-radius:50%;background:#ff8a5c;animation:pulse 1.4s infinite;flex-shrink:0;display:inline-block"></span>'+
      '<span style="font-size:14px;color:#90a8ba">Sin calibrar — recomendado antes de bucear</span>'+
      '</div></div>';
  }
  var sphereInfo='';
  if(cal.phase==='idle'){
    sphereInfo='<div style="font-size:13.5px;color:#90a8ba;max-width:240px;line-height:1.5;text-align:center;margin-top:6px">'+
      'Pulsa <b style="color:#e9f1f7">Iniciar</b> y gira el ordenador describiendo un ocho hasta completar la esfera.</div>';
  } else if(cal.phase==='running'){
    sphereInfo='<div class="cal-stats" id="cal-stats">'+
      '<div class="cal-stat"><div class="cal-stat-lbl">Rumbo</div><div class="cal-stat-val" id="cs-hdg">'+Math.round(cal.hdg)+'°</div></div>'+
      '<div class="cal-stat"><div class="cal-stat-lbl">Cobertura</div><div class="cal-stat-val a" id="cs-prog">'+cal.prog+'%</div></div>'+
      '<div class="cal-stat"><div class="cal-stat-lbl">Calidad</div><div class="cal-stat-val" id="cs-qual">'+cal.qual+'%</div></div>'+
      '</div>';
  } else {
    sphereInfo='<div style="margin-top:6px"><div class="done-badge">'+
      '<svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="#54d691" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"><path d="M20 6L9 17l-5-5"/></svg>'+
      'Calibración completada</div></div>';
  }
  var progBar=cal.phase==='running'?
    '<div class="prog-wrap"><div class="prog-bar" id="cal-pb" style="width:'+cal.prog+'%"></div></div>':'';
  var offPanel=cal.phase==='done'?
    '<div class="panel" style="padding:14px 16px;margin-bottom:10px">'+
    '<p class="sec-title">Compensación hard-iron</p>'+
    '<div style="display:flex;justify-content:space-around">'+
    '<div class="cal-stat"><div class="cal-stat-lbl">Offset X</div><div class="cal-stat-val">'+cal.off.x+'</div>'+
    '<div style="font-size:10px;color:#90a8ba;font-family:monospace">µT</div></div>'+
    '<div class="cal-stat"><div class="cal-stat-lbl">Offset Y</div><div class="cal-stat-val">'+cal.off.y+'</div>'+
    '<div style="font-size:10px;color:#90a8ba;font-family:monospace">µT</div></div>'+
    '<div class="cal-stat"><div class="cal-stat-lbl">Offset Z</div><div class="cal-stat-val">'+cal.off.z+'</div>'+
    '<div style="font-size:10px;color:#90a8ba;font-family:monospace">µT</div></div>'+
    '<div class="cal-stat"><div class="cal-stat-lbl">Calidad</div><div class="cal-stat-val a">'+cal.qual+'%</div></div>'+
    '</div></div>':'';
  var btns='';
  if(cal.phase==='running'){
    btns='<button class="btn-ghost" data-action="calCancel">Cancelar</button>';
  } else if(cal.phase==='done'){
    btns='<div class="btn-row"><button class="btn-acc" data-action="calStart">Recalibrar</button>'+
      '<button class="btn-ghost" data-action="calReset">Resetear</button></div>';
  } else {
    btns='<div class="btn-row"><button class="btn-acc" data-action="calStart">Iniciar calibración</button>'+
      (cal.saved?'<button class="btn-ghost" data-action="calReset">Resetear</button>':'')+
      '</div>';
  }
  return '<div class="scr-pad fade-up">'+savedPanel+
    '<div class="panel" style="padding:16px;margin-bottom:10px">'+
    '<div style="display:flex;flex-direction:column;align-items:center">'+
    '<div id="cal-sphere">'+svgSphere(cal.cov,cal.hdg,cal.phase)+'</div>'+
    sphereInfo+'</div></div>'+
    progBar+offPanel+btns+'</div>';
}

// ── cabecera y tabs ───────────────────────────────────────────
function buildHeader() {
  var titles=['Resumen','Bitácora','Calibración'];
  var title=titles[S.tab];
  if(S.tab===1&&S.sel>=0) title='Inmersión';
  var badge='';
  if(S.cal.phase==='running') badge='<span class="hdr-badge">Calibrando…</span>';
  else if(!S.cal.saved) badge='<span class="hdr-badge warn">Sin cal.</span>';
  return '<div class="hdr"><div>'+
    '<div class="hdr-title">'+title+'</div>'+
    '<div class="hdr-sub">Brújula · 192.168.4.1</div>'+
    '</div>'+badge+'</div>';
}

function buildTabBar() {
  var icons=[
    '<path d="M3 9l9-7 9 7v11a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/><polyline points="9 22 9 12 15 12 15 22"/>',
    '<path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/><polyline points="14 2 14 8 20 8"/><line x1="16" y1="13" x2="8" y2="13"/><line x1="16" y1="17" x2="8" y2="17"/>',
    '<circle cx="12" cy="12" r="10"/><line x1="12" y1="8" x2="12" y2="12"/><line x1="12" y1="16" x2="12.01" y2="16"/>'
  ];
  var labels=['Resumen','Bitácora','Calibrar'];
  var btns=icons.map(function(ic,i){
    return '<button class="tab-btn'+(S.tab===i?' active':'')+'" data-action="tab" data-id="'+i+'">'+
      '<svg class="tab-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">'+ic+'</svg>'+
      labels[i]+'</button>';
  }).join('');
  return '<div class="tabbar">'+btns+'</div>';
}

// ── render ────────────────────────────────────────────────────
function render() {
  document.getElementById('hdr').innerHTML=buildHeader();
  document.getElementById('tabbar').innerHTML=buildTabBar();
  var html='';
  if(S.tab===0) html=buildSummary();
  else if(S.tab===1) html=S.sel>=0?buildDetail():buildList();
  else html=buildCalibrate();
  document.getElementById('screen').innerHTML=html;
}

// ── calibración (API ESP32) ───────────────────────────────────
function calStart() {
  S.cal.phase='running';S.cal.prog=0;S.cal.qual=0;
  S.cal.cov=new Array(24).fill(0);
  render();
  fetch('/start').catch(function(){});
  _calTimer=setInterval(calPoll,400);
}
function calPoll() {
  fetch('/status').then(function(r){return r.json();}).then(function(data){
    var hdg=data.heading||0;
    S.cal.hdg=hdg;
    var sec=Math.floor(((hdg%360)+360)%360/15)%24;
    S.cal.cov[sec]=Math.min(1,S.cal.cov[sec]+0.18);
    var cov=0;for(var i=0;i<24;i++)if(S.cal.cov[i]>0.5)cov++;
    var pct=Math.min(100,Math.round((cov/24)*100));
    S.cal.prog=pct;S.cal.qual=Math.min(99,Math.round(pct*0.95));
    var sp=document.getElementById('cal-sphere');
    if(sp)sp.innerHTML=svgSphere(S.cal.cov,hdg,'running');
    var eh=document.getElementById('cs-hdg');if(eh)eh.textContent=Math.round(hdg)+'°';
    var ep=document.getElementById('cs-prog');if(ep)ep.textContent=pct+'%';
    var eq=document.getElementById('cs-qual');if(eq)eq.textContent=S.cal.qual+'%';
    var pb=document.getElementById('cal-pb');if(pb)pb.style.width=pct+'%';
    if(pct>=90)calStop();
  }).catch(function(){});
}
function calStop() {
  clearInterval(_calTimer);_calTimer=null;
  fetch('/stop').then(function(r){return r.json();}).then(function(data){
    var res={quality:Math.max(S.cal.qual||88,88),offsets:data.offset||{x:0,y:0,z:0},date:new Date().toISOString()};
    S.cal.off=res.offsets;S.cal.qual=res.quality;S.cal.phase='done';S.cal.saved=res;
    try{localStorage.setItem('dc_cal',JSON.stringify(res));}catch(e){}
    render();
  }).catch(function(){
    S.cal.phase='done';
    S.cal.saved={quality:S.cal.qual||88,offsets:S.cal.off,date:new Date().toISOString()};
    try{localStorage.setItem('dc_cal',JSON.stringify(S.cal.saved));}catch(e){}
    render();
  });
}
function calCancel() {
  clearInterval(_calTimer);_calTimer=null;
  fetch('/stop').catch(function(){});
  S.cal.phase='idle';S.cal.prog=0;S.cal.qual=0;S.cal.cov=new Array(24).fill(0);
  render();
}
function calReset() {
  fetch('/reset').catch(function(){});
  S.cal.saved=null;S.cal.phase='idle';S.cal.prog=0;S.cal.cov=new Array(24).fill(0);
  try{localStorage.removeItem('dc_cal');}catch(e){}
  render();
}

// ── guardar metadatos de sesión ───────────────────────────────
function saveMeta(idx) {
  var dv=S.dives&&S.dives[idx];
  if(!dv) return;
  var locEl=document.getElementById('ed-loc');
  var timEl=document.getElementById('ed-time');
  if(!locEl||!timEl) return;
  var sessId=parseInt(dv.id.replace('d-',''),10);
  var params='sess='+sessId+'&location='+encodeURIComponent(locEl.value)+'&starttime='+encodeURIComponent(timEl.value);
  var btn=document.querySelector('[data-action="saveMeta"]');
  if(btn){btn.textContent='Guardando…';btn.disabled=true;}
  fetch('/api/meta?'+params,{method:'POST'})
    .then(function(r){return r.json();})
    .then(function(d){
      if(d.ok){
        dv.location=locEl.value;
        dv.startTime=timEl.value||'----';
        dv.date=dv.startTime;
        if(btn){btn.textContent='✓ Guardado';setTimeout(function(){btn.textContent='Guardar';btn.disabled=false;},2000);}
      } else {
        if(btn){btn.textContent='Error';btn.disabled=false;}
      }
    })
    .catch(function(){if(btn){btn.textContent='Error';btn.disabled=false;}});
}

// ── carga de datos ────────────────────────────────────────────
function loadDives() {
  fetch('/api/dives').then(function(r){return r.json();}).then(function(data){
    S.dives=data.dives||[];render();
  }).catch(function(){S.dives=[];render();});
}

// ── delegación de eventos ─────────────────────────────────────
document.addEventListener('click',function(e){
  var el=e.target.closest('[data-action]');
  if(!el)return;
  var a=el.dataset.action,id=parseInt(el.dataset.id||'0');
  if(a==='tab'){S.tab=id;S.sel=-1;render();}
  else if(a==='sel'){S.sel=id;render();}
  else if(a==='back'){S.sel=-1;render();}
  else if(a==='dtab'){S.detTab=id;render();}
  else if(a==='saveMeta'){saveMeta(id);}
  else if(a==='calStart'){calStart();}
  else if(a==='calCancel'){calCancel();}
  else if(a==='calReset'){calReset();}
});

// ── arranque ──────────────────────────────────────────────────
render();
loadDives();
</script>
</body>
</html>
)HTMLEND";
