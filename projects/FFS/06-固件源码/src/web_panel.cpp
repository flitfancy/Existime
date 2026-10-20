#include "web_panel.h"
#include "heart_rate.h"
#include "phone_ble.h"
#include "tft_display.h"

#include <WebServer.h>
#include "ota_update.h"

namespace {

constexpr size_t kWebRows = 200;
String s_rows[kWebRows];
size_t s_head = 0;
size_t s_count = 0;
WebServer s_server(80);

const char kHtml[] PROGMEM = R"RAWHTML(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>FIREFLY-SENSE</title>
<style>
:root{--bg:#0b1220;--panel:#111c31;--panel2:#16233d;--line:#24344f;
  --text:#e6edf7;--muted:#8fa3c0;--ok:#34d399;--bad:#fb7185;
  --c1:#38bdf8;--c2:#34d399;--c3:#f472b6;--c4:#fbbf24;--c5:#a78bfa;}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);
  font:15px/1.45 system-ui,-apple-system,"Segoe UI",sans-serif;padding:16px}
.head{display:flex;align-items:center;gap:10px;margin-bottom:4px}
.head h1{font-size:16px;font-weight:600;margin:0;letter-spacing:.2px}
.dot{width:9px;height:9px;border-radius:50%;background:var(--ok);
  box-shadow:0 0 8px var(--ok)}
.head .sub{font-size:12px;color:var(--muted);margin-left:auto}
.head .sub b{color:var(--text);font-weight:500}
.grid{display:grid;grid-template-columns:repeat(2,1fr);
  gap:10px;margin:14px 0 18px}
@media(min-width:640px){.grid{grid-template-columns:repeat(3,1fr)}}
@media(min-width:960px){.grid{grid-template-columns:repeat(6,1fr)}}
.card{background:linear-gradient(160deg,var(--panel),var(--panel2));
  border:1px solid var(--line);border-radius:12px;padding:12px 13px;
  position:relative;overflow:hidden}
.card::before{content:"";position:absolute;left:0;top:0;bottom:0;width:3px;
  background:var(--acc,var(--c1));opacity:.8}
.card .name{font-size:12px;color:var(--muted);letter-spacing:.4px;
  text-transform:uppercase}
.card .val{font-size:22px;font-weight:600;margin:6px 0 2px;
  font-variant-numeric:tabular-nums;white-space:nowrap}
.card .meta{font-size:12px;color:var(--muted);display:flex;gap:6px;
  align-items:center}
.flag{width:7px;height:7px;border-radius:50%;background:var(--ok)}
.flag.bad{background:var(--bad)}
.charts{display:grid;gap:12px}
.chart{background:var(--panel);border:1px solid var(--line);border-radius:12px;
  padding:10px 12px 8px}
.chart .t{display:flex;align-items:baseline;gap:8px;margin-bottom:6px}
.chart .t h2{font-size:14px;margin:0;font-weight:500;color:var(--text)}
.chart .t span{font-size:12px;color:var(--muted)}
canvas{width:100%;height:175px;display:block;background:#0a1322;
  border-radius:8px}
</style>
</head>
<body>
<div class="head">
  <span class="dot" id="dot"></span>
  <h1>FIREFLY-SENSE</h1>
  <a href="/update" style="color:var(--c1);font-size:12px">固件升级</a>
  <span class="sub" id="stamp">连接中…</span>
</div>
<div class="grid" id="cards"></div>
<div class="charts" id="charts"></div>
<script>
const S=['CH0 SHT41','CH1 BMP580','CH2 AS7341','CH3 LTR390','CH4 SGP41','CH5 SCD40'];
const COLS=['pc_time','uptime_ms','cycle','channel','sensor','ok','temp_c','rh_pct','als_raw','uv_raw',
 'f1_415','f2_445','f3_480','f4_515','f5_555','f6_590','f7_630','f8_680','clear_raw','nir_raw',
 'voc_index','nox_index','sraw_voc','sraw_nox','co2_ppm','pressure_pa','as7341_atime','as7341_astep','as7341_gainx',
 'sample_age_ms','sample_seq','error_streak','firmware_version','schema_version','scheduler','flicker_hz','rssi_dbm'];
const ACC=['var(--c1)','var(--c2)','var(--c3)','var(--c4)','var(--c5)','var(--c1)'];
const PAIRS=[['temp_c','rh_pct'],['pressure_pa','temp_c'],['clear_raw','nir_raw'],
  ['als_raw','uv_raw'],['voc_index','sraw_voc'],['co2_ppm','temp_c']];
const UNITS=[['°C','%RH'],['Pa','°C'],['raw','raw'],['raw','raw'],['idx','raw'],['ppm','°C']];
let hist=[];
function parse(l){const p=l.split(','),o={};COLS.forEach((c,i)=>o[c]=p[i]!==undefined?p[i]:'NA');return o;}
function num(v){const n=parseFloat(v);return isFinite(n)?n:NaN;}
function fmt(n,d){return isFinite(n)?n.toFixed(d):'—';}
function text(i,r){switch(i){
 case 0:return fmt(num(r.temp_c),2)+'°C · '+fmt(num(r.rh_pct),1)+'%';
 case 1:return fmt(num(r.pressure_pa),0)+' Pa';
 case 2:return 'CLR '+num(r.clear_raw)+' · NIR '+num(r.nir_raw)+' · '+r.as7341_gainx+'x';
 case 3:return 'ALS '+num(r.als_raw)+' · UV '+num(r.uv_raw);
 case 4:return 'VOC '+r.voc_index+' · NOx '+r.nox_index;
 case 5:return fmt(num(r.co2_ppm),0)+' ppm';}
 return '';}
function extra(i,r){switch(i){
 case 1:return fmt(num(r.temp_c),2)+'°C';
 case 4:return 'SRAW '+r.sraw_voc+'/'+r.sraw_nox;
 case 5:return fmt(num(r.temp_c),1)+'°C · '+fmt(num(r.rh_pct),1)+'%';
 case 0:return '';case 2:return '';case 3:return '';}
 return '';}
function card(i,r){const d=document.createElement('div');d.className='card';
 d.style.setProperty('--acc',ACC[i]);
 d.innerHTML='<div class="name">'+S[i]+'</div><div class="val">'+
  (r?text(i,r):'—')+'</div><div class="meta"><span class="flag'+
  (r&&r.ok!=='1'?' bad':'')+'"></span>'+
  (r?(extra(i,r)?extra(i,r)+' · ':'')+(r.pc_time?r.pc_time.slice(11,19):''):'等待数据')+
  '</div>';return d;}
function chart(i){const w=document.createElement('div');w.className='chart';
 const ks=PAIRS[i];
 w.innerHTML='<div class="t"><h2>'+S[i]+'</h2><span>'+ks[0]+' / '+ks[1]+'</span></div><canvas></canvas>';
 const cv=w.querySelector('canvas'),ctx=cv.getContext('2d');
 const draw=()=>{const W=cv.width=cv.clientWidth*devicePixelRatio,
   H=cv.height=175*devicePixelRatio;
  ctx.clearRect(0,0,W,H);ctx.setTransform(1,0,0,1,0,0);
  const rows=hist.filter(r=>r.sensor===S[i]).slice(-40);
  const series=ks.map(k=>rows.map(r=>num(r[k])).filter(v=>isFinite(v)));
  const all=[].concat(...series);if(!all.length)return;
  const mn=Math.min(...all),mx=Math.max(...all),sp=(mx-mn)||1;
  const pad=6*devicePixelRatio;
  ctx.strokeStyle='rgba(148,163,184,.14)';ctx.lineWidth=1;
  for(let g=1;g<4;g++){const y=pad+(H-2*pad)*g/4;ctx.beginPath();
   ctx.moveTo(0,y);ctx.lineTo(W,y);ctx.stroke();}
  const cs=[getComputedStyle(document.documentElement).getPropertyValue('--c1').trim(),
    getComputedStyle(document.documentElement).getPropertyValue('--c2').trim()];
  series.forEach((arr,si)=>{ctx.strokeStyle=cs[si];ctx.lineWidth=2.5*devicePixelRatio;
   ctx.lineJoin='round';ctx.beginPath();
   arr.forEach((v,xi)=>{const x=pad+(W-2*pad)*(arr.length<2?.5:xi/(arr.length-1));
    const y=H-pad-(v-mn)/sp*(H-2*pad);
    xi?ctx.lineTo(x,y):ctx.moveTo(x,y);});ctx.stroke();});};
 draw();new ResizeObserver(draw).observe(cv);return w;}
async function poll(){try{
  const r=await fetch('/data');const rows=await r.json();
  if(!rows||!rows.length){document.getElementById('stamp').textContent='已连接，等待数据…';return;}
  hist=rows.map(parse);const latest={};hist.forEach(r=>latest[r.sensor]=r);
  const box=document.getElementById('cards');box.innerHTML='';
  S.forEach((n,i)=>box.appendChild(card(i,latest[n])));
  const ch=document.getElementById('charts');ch.innerHTML='';
  S.forEach((n,i)=>ch.appendChild(chart(i)));
  const last=hist[hist.length-1];
  document.getElementById('dot').style.background='var(--ok)';
  document.getElementById('stamp').innerHTML='更新 <b>'+
    (last.pc_time?last.pc_time.slice(11,19):'')+'</b> · '+hist.length+' 行';
 }catch(e){document.getElementById('dot').style.background='var(--bad)';
  document.getElementById('stamp').textContent='连接失败，重试…';}}
poll();setInterval(poll,2000);
</script>
</body>
</html>
)RAWHTML";

void handleRoot() {
  s_server.send_P(200, "text/html; charset=utf-8", kHtml);
}

void handleData() {
  String body;
  body.reserve(s_count * 220);
  body += '[';
  for (size_t i = 0; i < s_count; ++i) {
    const size_t idx = (s_head + kWebRows - s_count + i) % kWebRows;
    if (i > 0) {
      body += ',';
    }
    body += '"';
    body += s_rows[idx];
    body += '"';
  }
  body += ']';
  s_server.send(200, "application/json; charset=utf-8", body);
}

}  // namespace

void webPanelSetup() {
  s_server.on("/", handleRoot);
  s_server.on("/data", handleData);
  otaSetup(s_server);
  s_server.on("/display", HTTP_GET, [] {
    s_server.sendHeader("Cache-Control", "no-store");
    s_server.send(200, "application/json; charset=utf-8", tftDisplayJson());
  });
  s_server.on("/display/command", HTTP_POST, [] {
    s_server.sendHeader("Cache-Control", "no-store");
    if (!otaAuthorizeControl()) {
      s_server.send(403, "application/json", "{\"error\":\"request-rejected\"}");
      return;
    }
    String command = s_server.arg("plain");
    command.trim();
    command.toUpperCase();
    if (command.length() > 40 || !command.startsWith("TFT")) {
      s_server.send(400, "application/json", "{\"error\":\"invalid-display-command\"}");
      return;
    }
    tftDisplayCommand(command);
    s_server.send(200, "application/json; charset=utf-8", tftDisplayJson());
  });
  s_server.on("/heart-rate", [] {
    s_server.sendHeader("Cache-Control", "no-store");
    s_server.send(200, "application/json; charset=utf-8", heartRateJson());
  });
  s_server.on("/phone-ble", [] {
    s_server.sendHeader("Cache-Control", "no-store");
    s_server.send(200, "application/json; charset=utf-8", phoneBleJson());
  });
  s_server.begin();
  Serial.println("[web] panel on port 80 (http://<esp-ip>/).");
}

void webPanelTick() {
  s_server.handleClient();
  otaTick();
}

void webPanelAddRow(const String& line) {
  s_rows[s_head] = line;
  s_head = (s_head + 1) % kWebRows;
  if (s_count < kWebRows) {
    ++s_count;
  }
}

size_t webPanelSnapshotBytes() {
  size_t bytes = 0;
  for (size_t i = 0; i < s_count; ++i) bytes += s_rows[(s_head + kWebRows - s_count + i) % kWebRows].length() + 1;
  return bytes;
}

bool webPanelCopyRows(char* buffer, size_t capacity, size_t* offsets, size_t maxRows,
                     size_t& rows, uint32_t& uptime) {
  if (!buffer || !offsets || maxRows < s_count) return false;
  uptime = millis(); rows = s_count;
  size_t written = 0;
  for (size_t i = 0; i < s_count; ++i) {
    const String& line = s_rows[(s_head + kWebRows - s_count + i) % kWebRows];
    if (line.length() == 0 || line.length() > 2048 || capacity - written < line.length() + 1) return false;
    offsets[i] = written;
    memcpy(buffer + written, line.c_str(), line.length() + 1);
    written += line.length() + 1;
  }
  return true;
}
