/*
 * osc_web.cpp - WebServer + OTA (ESP32 connectivity core)
 *
 * Turns the ESP32 into a self-contained web oscilloscope.
 *
 * Architecture:
 *   WiFi STA (credentials in osc_wifi.cpp, overridable from the web page)
 *     -> DHCP IP, or the setup AP at 192.168.4.1 when nothing connects
 *   Browser -> http://<ip>/        -> embedded HTML scope page
 *   Browser polls /wave (10 Hz, binary 1200 B), /stat + /meas (2 Hz, JSON)
 *   Browser -> /cmd?action=...     -> osc_proto_send -> STM32
 *   Browser -> /wifi (GET status, POST new SSID/password -> NVS, next boot)
 *   CSV export via /csv
 *   OTA via ArduinoOTA (Arduino IDE / PlatformIO remote upload)
 *
 * Data flow:
 *   main loop -> osc_proto_recv -> cache STAT/MEAS/WAVE -> web clients
 *   web clients -> /cmd -> osc_proto_send -> STM32
 */
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include "osc_web.h"
#include "osc_proto.h"
#include "osc_wifi.h"

/* WiFi credentials live in osc_wifi.cpp (two built-in arrays + NVS override) */
#define WIFI_REBOOT_DELAY_MS  700u

/* rate table — must match STM32 k_rates[] (main.c:44-48) */
static const uint32_t k_rates[13] = {
    100, 200, 500, 1000, 2000, 5000, 10000,
    20000, 50000, 100000, 200000, 500000, 1000000
};

/* ==================================================================
 * frame cache (written by main loop, read by web handlers)
 * ================================================================== */
static uint8_t  s_stat_cache[16];
static uint16_t s_stat_len = 0;
static uint8_t  s_meas_cache[24];
static uint16_t s_meas_len = 0;
static uint8_t  s_wave_cache[1200];
static uint16_t s_wave_len = 0;

void osc_web_cache_stat(const uint8_t *data, uint16_t len)
{
    if (len > sizeof(s_stat_cache)) len = sizeof(s_stat_cache);
    memcpy(s_stat_cache, data, len);
    s_stat_len = len;
}

void osc_web_cache_meas(const uint8_t *data, uint16_t len)
{
    if (len > sizeof(s_meas_cache)) len = sizeof(s_meas_cache);
    memcpy(s_meas_cache, data, len);
    s_meas_len = len;
}

void osc_web_cache_wave(const uint8_t *data, uint16_t len)
{
    if (len > sizeof(s_wave_cache)) len = sizeof(s_wave_cache);
    memcpy(s_wave_cache, data, len);
    s_wave_len = len;
}

/* ==================================================================
 * WebServer instance
 * ================================================================== */
static WebServer s_server(80);

/* deferred reboot: /cmd?action=reboot and /cmd?action=wifireset send their
   response first, then restart from osc_web_poll() so the reply is flushed */
static uint32_t s_reboot_at = 0u;

/* little-endian readers (STM32 is LE) */
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

/* ==================================================================
 * route: /  — embedded HTML scope page
 * ================================================================== */
static const char PAGE_HTML[] = R"====(
<!DOCTYPE html>
<html lang="zh-CN"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="theme-color" content="#0a0e13">
<title>DualCore Scope</title>
<style>
*{box-sizing:border-box}
:root{--bg:#0a0e13;--card:#131c26;--card2:#0f1720;--line:#243140;--fg:#dce7f2;--dim:#7a8b9e;
--ch1:#2ee86b;--ch2:#ffc23d;--trig:#ff5a52;--run:#2ee86b;--stop:#ffa02e}
html,body{margin:0;background:var(--bg);color:var(--fg);
font:14px/1.45 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,"Noto Sans SC","Microsoft YaHei",sans-serif}
.wrap{max-width:1000px;margin:0 auto;padding:10px 12px 28px}
header{display:flex;align-items:center;gap:10px;padding:2px 0 10px}
h1{margin:0;font-size:16px;font-weight:600;letter-spacing:.3px}
h1 i{font-style:normal;color:var(--dim);font-size:12px;font-weight:400;margin-left:7px}
#meta{margin-left:auto;font-size:12px;color:var(--dim);font-variant-numeric:tabular-nums;text-align:right}
.pill{font-weight:700;font-size:12px;padding:3px 10px;border-radius:99px;background:var(--card);
border:1px solid var(--line);color:var(--dim);min-width:58px;text-align:center}
.pill.run{color:#06140b;background:var(--run);border-color:var(--run)}
.pill.stop{color:#1a0f00;background:var(--stop);border-color:var(--stop)}
.scope{position:relative;background:#000;border:1px solid var(--line);border-radius:10px;overflow:hidden}
canvas{display:block;width:100%;touch-action:none;cursor:crosshair}
#lvltag{position:absolute;right:8px;transform:translateY(-50%);pointer-events:none;
font:11px/1 ui-monospace,Consolas,monospace;color:#ffb3ae;background:rgba(0,0,0,.62);
padding:3px 6px;border-radius:4px;border:1px solid rgba(255,90,82,.45)}
.hint{font-size:11px;color:var(--dim);padding:6px 2px 0}
.bar{display:flex;flex-wrap:wrap;gap:8px;align-items:center;margin-top:10px}
.grp{display:flex;align-items:center;gap:6px;background:var(--card);border:1px solid var(--line);
border-radius:8px;padding:5px 8px}
.grp>label{font-size:12px;color:var(--dim)}
select,button{font:inherit;font-size:13px;color:var(--fg);background:var(--card2);
border:1px solid var(--line);border-radius:6px;padding:4px 7px}
button{cursor:pointer}
button:active{transform:translateY(1px)}
button.primary{background:#12351f;border-color:#1e6b3f;color:#9df3bd}
button.danger{background:#3a1a12;border-color:#6b2f1e;color:#ffbba5}
input[type=range]{width:118px;accent-color:var(--trig)}
#tlv{font-size:12px;color:var(--dim);min-width:58px;text-align:right;font-variant-numeric:tabular-nums}
a.btn{text-decoration:none;font-size:13px;padding:5px 11px;border:1px solid var(--line);
border-radius:6px;background:var(--card2);color:var(--fg)}
.cards{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-top:8px}
@media(max-width:620px){.cards{grid-template-columns:1fr}}
.card{background:var(--card);border:1px solid var(--line);border-left:3px solid var(--dim);
border-radius:8px;padding:8px 10px}
.card.c1{border-left-color:var(--ch1)}
.card.c2{border-left-color:var(--ch2)}
.card h2{margin:0 0 6px;font-size:12px;letter-spacing:.5px}
.card.c1 h2{color:var(--ch1)}
.card.c2 h2{color:var(--ch2)}
.kv{display:grid;grid-template-columns:1fr 1fr;gap:3px 12px}
.kv div{display:flex;justify-content:space-between;gap:6px;font-variant-numeric:tabular-nums}
.kv em{font-style:normal;color:var(--dim);font-size:12px}
.kv b{font-weight:600}
details.wifi{background:var(--card);border:1px solid var(--line);border-radius:8px;
margin-top:8px;padding:0 10px}
details.wifi>summary{cursor:pointer;font-size:12px;color:var(--dim);padding:9px 0;
list-style:none;user-select:none;outline:none}
details.wifi>summary::-webkit-details-marker{display:none}
details.wifi>summary::before{content:"▸ "}
details.wifi[open]>summary::before{content:"▾ "}
.wrow{display:flex;flex-wrap:wrap;gap:8px;align-items:center;padding:2px 0 8px}
.wrow>label{font-size:12px;color:var(--dim)}
.wrow input{font:inherit;font-size:13px;color:var(--fg);background:var(--card2);
border:1px solid var(--line);border-radius:6px;padding:4px 7px}
#wssid{width:150px}
#wpass{width:190px}
#wmsg{font-size:12px;color:var(--dim)}
#wmsg.ok{color:#7ee2a8}
#wmsg.bad{color:#ff9b8a}
#winfo{font-size:11px;color:var(--dim);padding:0 0 11px;line-height:1.6}
#apbar{display:none;margin-top:8px;padding:10px 12px;border-radius:8px;
background:#3a1a12;border:1px solid #6b2f1e;color:#ffd7cc;font-size:13px;line-height:1.75}
#apbar.on{display:block}
#apbar b{color:#fff}
#apbar code{background:rgba(0,0,0,.35);padding:1px 5px;border-radius:4px;color:#ffd7cc}
</style></head><body><div class="wrap">
<header>
<h1>DualCore Scope<i>双核示波器</i></h1>
<span id="meta">等待 STM32 数据…</span>
<span class="pill" id="pill">--</span>
</header>
<div id="apbar"></div>
<div class="scope"><canvas id="cv"></canvas><div id="lvltag">--</div></div>
<div class="hint">提示：在波形上按住拖动即可设置触发电平</div>
<div class="bar">
<div class="grp"><button class="primary" id="bRun">&#9654; 运行</button><button class="danger" id="bStop">&#9632; 停止</button></div>
<div class="grp"><label>时基</label><select id="tb"></select></div>
<div class="grp"><label>触发</label>
<select id="tm"><option value="0">自动</option><option value="1">常态</option><option value="2">单次</option></select>
<select id="te"><option value="0">上升沿</option><option value="1">下降沿</option></select>
<select id="tc"><option value="0">CH1</option><option value="1">CH2</option></select></div>
<div class="grp"><label>电平</label><input type="range" id="tl" min="0" max="4095" value="2048"><span id="tlv">--</span></div>
<a class="btn" href="/csv">下载 CSV</a>
</div>
<div class="cards">
<div class="card c1"><h2>CH1</h2><div class="kv">
<div><em>波峰</em><b id="p1">--</b></div><div><em>平均</em><b id="a1">--</b></div>
<div><em>频率</em><b id="f1">--</b></div><div><em>占空比</em><b id="d1">--</b></div></div></div>
<div class="card c2"><h2>CH2</h2><div class="kv">
<div><em>波峰</em><b id="p2">--</b></div><div><em>平均</em><b id="a2">--</b></div>
<div><em>频率</em><b id="f2">--</b></div><div><em>占空比</em><b id="d2">--</b></div></div></div>
</div>
<details class="wifi" id="wpanel">
<summary>WiFi 设置（名称 / 密码）</summary>
<div class="wrow">
<label for="wssid">名称</label><input id="wssid" type="text" maxlength="32" placeholder="WiFi 名称">
<label for="wpass">密码</label><input id="wpass" type="password" maxlength="63" placeholder="留空 = 不修改密码">
<button class="primary" id="bWifi">保存 WiFi</button>
<button id="bReboot">重启生效</button>
<button class="danger" id="bWreset">恢复默认</button>
<span id="wmsg"></span>
</div>
<div id="winfo">--</div>
</details>
</div>
<script>
var W=300,H=150,N=300;
var RATES=["100 S/s","200 S/s","500 S/s","1 kS/s","2 kS/s","5 kS/s","10 kS/s","20 kS/s",
"50 kS/s","100 kS/s","200 kS/s","500 kS/s","1 MS/s"];
var MODE=["自动","常态","单次"],EDGE=["上升沿","下降沿"];
function $(i){return document.getElementById(i)}
var cv=$('cv'),cx=cv.getContext('2d'),tag=$('lvltag');
var dpr=Math.min(window.devicePixelRatio||1,2);
var cssW=300,unit=1,lw=1,jaw=null,level=2048,drag=false,lastLocal=0,lastSend=0;

function initCanvas(){
  cssW=cv.parentNode.clientWidth||300;
  var cssH=Math.round(cssW*H/W);
  cv.style.height=cssH+'px';
  cv.width=Math.max(1,Math.round(cssW*dpr));
  cv.height=Math.max(1,Math.round(cssH*dpr));
  var s=cv.width/W;
  cx.setTransform(s,0,0,s,0,0);
  unit=W/cssW;lw=1.15*unit;
  render();
}
window.addEventListener('resize',initCanvas);

function y2(v){return H-(v*H/4096)}

function grid(){
  cx.fillStyle='#000';cx.fillRect(0,0,W,H);
  cx.setLineDash([]);cx.lineWidth=lw;
  cx.strokeStyle='#16301c';cx.beginPath();
  for(var x=0;x<=W;x+=30){cx.moveTo(x,0);cx.lineTo(x,H)}
  for(var y=0;y<=H;y+=25){cx.moveTo(0,y);cx.lineTo(W,y)}
  cx.stroke();
  cx.strokeStyle='#2b5a34';cx.beginPath();
  cx.moveTo(150,0);cx.lineTo(150,H);cx.moveTo(0,75);cx.lineTo(W,75);cx.stroke();
}
function trace(buf,off,col){
  cx.strokeStyle=col;cx.lineWidth=lw*1.4;cx.lineJoin='round';cx.lineCap='round';cx.beginPath();
  for(var i=0;i<N;i++){var v=buf.getUint16(off+i*2,true);var y=y2(v);
    if(i)cx.lineTo(i,y);else cx.moveTo(i,y)}
  cx.stroke();
}
function trigLine(){
  var y=y2(level);
  cx.strokeStyle='rgba(255,90,82,.85)';cx.lineWidth=lw;cx.setLineDash([6*unit,5*unit]);
  cx.beginPath();cx.moveTo(0,y);cx.lineTo(W,y);cx.stroke();cx.setLineDash([]);
  cx.fillStyle='rgba(255,90,82,.9)';cx.beginPath();
  cx.moveTo(0,y);cx.lineTo(7*unit,y-4*unit);cx.lineTo(7*unit,y+4*unit);cx.closePath();cx.fill();
  tag.style.top=((y/H)*(cv.clientHeight||H))+'px';
  tag.textContent=(level*3300/4096).toFixed(0)+' mV';
}
function render(){
  grid();
  if(jaw){trace(jaw,0,'#2ee86b');trace(jaw,600,'#ffc23d')}
  trigLine();
}

function wait(){$('pill').textContent='--';$('pill').className='pill';
  $('meta').textContent='等待 STM32 数据…'}
function fail(){$('meta').textContent='连接中断，正在重试…'}

function fWave(){
  if(document.hidden)return;
  fetch('/wave',{cache:'no-store'}).then(function(r){
    if(r.status===503){wait();return null}
    if(!r.ok)throw 0;
    return r.arrayBuffer();
  }).then(function(b){
    if(!b||b.byteLength<1200)return;
    jaw=new DataView(b);render();
  }).catch(fail);
}
function fStat(){
  if(document.hidden)return;
  fetch('/stat',{cache:'no-store'}).then(function(r){
    if(r.status===503){wait();return null}
    if(!r.ok)throw 0;
    return r.json();
  }).then(function(d){
    if(!d)return;
    $('pill').textContent=d.running?'运行':'停止';
    $('pill').className='pill '+(d.running?'run':'stop');
    $('meta').textContent=RATES[d.rate_idx]+' · '+MODE[d.mode]+' · '+EDGE[d.edge]+' · CH'+(d.ch+1);
    if(Date.now()-lastLocal>1500){
      $('tb').value=d.rate_idx;$('tm').value=d.mode;$('te').value=d.edge;$('tc').value=d.ch;
      setLevel(d.level,false);
    }
  }).catch(fail);
}
function fMeas(){
  if(document.hidden)return;
  fetch('/meas',{cache:'no-store'}).then(function(r){
    if(r.status===503){wait();return null}
    if(!r.ok)throw 0;
    return r.json();
  }).then(function(d){
    if(!d)return;
    fill(1,d.ch1);fill(2,d.ch2);
  }).catch(fail);
}
function mv(v){return v>=1000?(v/1000).toFixed(2)+' V':v.toFixed(0)+' mV'}
function hz(v){var h=v/100;return h>=1000?(h/1000).toFixed(3)+' kHz':h.toFixed(1)+' Hz'}
function fill(i,c){
  $('p'+i).textContent=mv(c.vpp);
  $('a'+i).textContent=mv(c.vavg);
  $('f'+i).textContent=hz(c.freq);
  $('d'+i).textContent=(c.duty/10).toFixed(1)+' %';
}

function cmd(a){lastLocal=Date.now();fetch('/cmd?action='+a,{cache:'no-store'}).catch(fail)}
function sendTrig(force){
  if(!force&&Date.now()-lastSend<120)return;
  lastSend=Date.now();
  fetch('/cmd?action=trig&mode='+$('tm').value+'&edge='+$('te').value+
        '&ch='+$('tc').value+'&level='+level,{cache:'no-store'}).catch(fail);
}
function setLevel(v,send){
  level=v<0?0:(v>4095?4095:v);
  $('tl').value=level;
  $('tlv').textContent=(level*3300/4096).toFixed(0)+' mV';
  render();
  if(send)  {lastLocal=Date.now();sendTrig(false)}
}

function posY(e){
  var r=cv.getBoundingClientRect();
  return (e.clientY-r.top)/r.height*H;
}
cv.addEventListener('pointerdown',function(e){
  drag=true;
  if(cv.setPointerCapture)cv.setPointerCapture(e.pointerId);
  setLevel(Math.round((H-posY(e))*4096/H),true);
});
cv.addEventListener('pointermove',function(e){
  if(drag)setLevel(Math.round((H-posY(e))*4096/H),true);
});
cv.addEventListener('pointerup',function(){if(drag){drag=false;sendTrig(true)}});
cv.addEventListener('pointercancel',function(){drag=false});

$('bRun').onclick=function(){cmd('start')};
$('bStop').onclick=function(){cmd('stop')};
$('tb').onchange=function(){lastLocal=Date.now();cmd('tb&val='+this.value)};
$('tm').onchange=$('te').onchange=$('tc').onchange=function(){lastLocal=Date.now();sendTrig(true)};
$('tl').oninput=function(){setLevel(+this.value,true)};

function fWifi(){
  fetch('/wifi',{cache:'no-store'}).then(function(r){if(!r.ok)throw 0;return r.json()})
  .then(function(d){
    if(document.activeElement!==$('wssid'))$('wssid').value=d.ssid;
    $('wpass').value='';
    $('winfo').textContent='当前：'+d.ssid+(d.custom?'（已自定义）':'（内置默认）')+
      ' · 密码'+(d.has_pass?'已设置':'无')+' · IP '+d.ip+
      (d.ap?' — 未连上路由器，请在此填写新的 WiFi 后点“重启生效”':'');
    var bar=$('apbar');
    if(d.ap){
      bar.className='on';
      bar.innerHTML='<b>设备未连接路由器，已自动开启热点</b><br>'+
        '手机连接热点 <code>'+(d.ap_ssid||'Oscilloscope-Setup')+'</code>（密码 <code>12345678</code>），'+
        '然后在本页下方填写家里的 WiFi 名称与密码 → 点「保存 WiFi」→ 点「重启生效」。';
      $('wpanel').open=true;
    }else{
      bar.className='';bar.innerHTML='';
    }
  }).catch(function(){$('winfo').textContent='无法读取 WiFi 状态'});
}
$('bWifi').onclick=function(){
  var s=$('wssid').value;
  if(!s){$('wmsg').className='bad';$('wmsg').textContent='请填写 WiFi 名称';return}
  var b='ssid='+encodeURIComponent(s);
  if($('wpass').value)b+='&pass='+encodeURIComponent($('wpass').value);
  $('wmsg').className='';$('wmsg').textContent='保存中…';
  fetch('/wifi',{method:'POST',cache:'no-store',
        headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b})
  .then(function(r){return r.json().catch(function(){return{ok:0,msg:'HTTP '+r.status}})})
  .then(function(d){
    $('wmsg').className=d.ok?'ok':'bad';
    $('wmsg').textContent=d.msg||(d.ok?'已保存':'保存失败');
    if(d.ok){$('wpass').value='';fWifi()}
  }).catch(function(){$('wmsg').className='bad';$('wmsg').textContent='保存失败：连接中断'});
};
$('bReboot').onclick=function(){
  if(confirm('立即重启 ESP32？\n设备会断开约 5 秒，之后使用新保存的 WiFi。'))cmd('reboot');
};
$('bWreset').onclick=function(){
  if(confirm('清除已保存的 WiFi，恢复为代码里的内置默认值并重启？'))cmd('wifireset');
};

RATES.forEach(function(l,i){
  var o=document.createElement('option');o.value=i;o.textContent=l;$('tb').appendChild(o);
});
initCanvas();
setInterval(fWave,100);setInterval(fStat,500);setInterval(fMeas,500);
fStat();fMeas();fWave();fWifi();
</script></body></html>
)====";

static void handleRoot()
{
    s_server.send(200, "text/html; charset=utf-8", PAGE_HTML);
}

/* ==================================================================
 * route: /stat  -> JSON
 * STAT payload (11 B): running, rate_idx, mode, edge, ch, level(u16), rate(u32)
 * ================================================================== */
static void handleStat()
{
    if (s_stat_len < 11)
    {
        s_server.send(503, "text/plain", "no data");
        return;
    }
    const uint8_t *p = s_stat_cache;
    uint16_t level = rd16(p + 5);
    uint32_t rate  = rd32(p + 7);
    char buf[160];
    snprintf(buf, sizeof(buf),
             "{\"running\":%u,\"rate_idx\":%u,\"rate\":%lu,"
             "\"mode\":%u,\"edge\":%u,\"ch\":%u,\"level\":%u}",
             p[0], p[1], (unsigned long)rate,
             p[2], p[3], p[4], level);
    s_server.sendHeader("Cache-Control", "no-store");
    s_server.send(200, "application/json", buf);
}

/* ==================================================================
 * route: /meas  -> JSON
 * MEAS payload (20 B): CH1[vpp(u16) vavg(u16) freq_x100(u32) duty_x10(u16)]
 *                       CH2[same]
 * ================================================================== */
static void handleMeas()
{
    if (s_meas_len < 20)
    {
        s_server.send(503, "text/plain", "no data");
        return;
    }
    const uint8_t *p = s_meas_cache;
    char buf[256];
    snprintf(buf, sizeof(buf),
             "{\"ch1\":{\"vpp\":%u,\"vavg\":%u,\"freq\":%lu,\"duty\":%u},"
             "\"ch2\":{\"vpp\":%u,\"vavg\":%u,\"freq\":%lu,\"duty\":%u}}",
             rd16(p + 0), rd16(p + 2), (unsigned long)rd32(p + 4), rd16(p + 8),
             rd16(p + 10), rd16(p + 12), (unsigned long)rd32(p + 14), rd16(p + 18));
    s_server.sendHeader("Cache-Control", "no-store");
    s_server.send(200, "application/json", buf);
}

/* ==================================================================
 * route: /wave  -> binary 1200 B (300 x u16 CH1 + 300 x u16 CH2, LE)
 * ================================================================== */
static void handleWave()
{
    if (s_wave_len < 1200)
    {
        s_server.send(503, "text/plain", "no data yet");
        return;
    }
    s_server.sendHeader("Cache-Control", "no-store");
    /* WebServer in this arduino-esp32 version has no binary send() overload;
     * String(const char*, size_t) preserves embedded NUL bytes and its
     * length() returns 1200, so Content-Length is correct. */
    s_server.send(200, "application/octet-stream",
                  String((const char *)s_wave_cache, 1200));
}

/* ==================================================================
 * route: /csv  -> downloadable CSV (300 rows)
 * ================================================================== */
static void handleCsv()
{
    if (s_wave_len < 1200)
    {
        s_server.send(503, "text/plain", "no data yet");
        return;
    }
    String csv;
    csv.reserve(8192);
    csv += "Sample,CH1_raw,CH1_mV,CH2_raw,CH2_mV\n";
    for (int i = 0; i < 300; i++)
    {
        uint16_t v1 = rd16(s_wave_cache + (size_t)i * 2u);
        uint16_t v2 = rd16(s_wave_cache + 600u + (size_t)i * 2u);
        uint32_t m1 = (uint32_t)v1 * 3300u / 4096u;
        uint32_t m2 = (uint32_t)v2 * 3300u / 4096u;
        char line[48];
        snprintf(line, sizeof(line), "%d,%u,%lu,%u,%lu\n",
                 i, v1, (unsigned long)m1, v2, (unsigned long)m2);
        csv += line;
    }
    s_server.sendHeader("Content-Disposition",
                        "attachment; filename=\"scope_data.csv\"");
    s_server.send(200, "text/csv", csv);
}

/* ==================================================================
 * route: /cmd?action=...  -> forward to STM32 via proto
 * ================================================================== */
static void handleCmd()
{
    String action = s_server.arg("action");
    if (action == "start")
        osc_proto_send(PROTO_CMD_START, NULL, 0);
    else if (action == "stop")
        osc_proto_send(PROTO_CMD_STOP, NULL, 0);
    else if (action == "reqwave")
        osc_proto_send(PROTO_CMD_REQ_WAVE, NULL, 0);
    else if (action == "reqstat")
        osc_proto_send(PROTO_CMD_REQ_STAT, NULL, 0);
    else if (action == "reqmeas")
        osc_proto_send(PROTO_CMD_REQ_MEAS, NULL, 0);
    else if (action == "tb")
    {
        uint8_t val = (uint8_t)s_server.arg("val").toInt();
        if (val < 13)
            osc_proto_send(PROTO_CMD_SET_TB, &val, 1);
    }
    else if (action == "trig")
    {
        uint8_t p[5];
        p[0] = (uint8_t)s_server.arg("mode").toInt();
        p[1] = (uint8_t)s_server.arg("edge").toInt();
        p[2] = (uint8_t)s_server.arg("ch").toInt();
        uint16_t lvl = (uint16_t)s_server.arg("level").toInt();
        p[3] = (uint8_t)(lvl & 0xFF);
        p[4] = (uint8_t)(lvl >> 8);
        osc_proto_send(PROTO_CMD_SET_TRIG, p, 5);
    }
    else if (action == "reboot")
    {
        s_server.send(200, "text/plain", "rebooting");
        s_reboot_at = millis() + WIFI_REBOOT_DELAY_MS;
        return;
    }
    else if (action == "wifireset")
    {
        osc_wifi_clear();
        s_server.send(200, "text/plain", "wifi reset, rebooting");
        s_reboot_at = millis() + WIFI_REBOOT_DELAY_MS;
        return;
    }
    else
    {
        s_server.send(400, "text/plain", "unknown action");
        return;
    }
    s_server.send(200, "text/plain", "ok");
}

/* ==================================================================
 * route: /wifi  — WiFi credentials
 *   GET  -> {"ssid","custom","has_pass","ap","ip"}
 *   POST ssid=..&pass=..  -> save to NVS (pass omitted = keep current).
 *   The new credentials are applied on the NEXT boot.
 * ================================================================== */

/* escape a string for JSON output (SSIDs may contain " or \) */
static void json_escape(const char *in, char *out, size_t n)
{
    size_t o = 0u;
    if (n == 0u) return;
    for (; (*in != '\0') && (o + 7u < n); in++)
    {
        unsigned char c = (unsigned char)*in;
        if ((c == '"') || (c == '\\'))
        {
            out[o++] = '\\';
            out[o++] = (char)c;
        }
        else if (c < 0x20u)
        {
            int w = snprintf(out + o, n - o, "\\u%04x", (unsigned)c);
            if (w <= 0) break;
            o += (size_t)w;
        }
        else
        {
            out[o++] = (char)c;
        }
    }
    out[o] = '\0';
}

static void handleWifiGet()
{
    char ssid[96];
    char buf[288];
    json_escape(osc_wifi_ssid(), ssid, sizeof(ssid));
    snprintf(buf, sizeof(buf),
             "{\"ssid\":\"%s\",\"custom\":%u,\"has_pass\":%u,\"ap\":%u,\"ap_ssid\":\"%s\",\"ip\":\"%s\"}",
             ssid, (unsigned)osc_wifi_custom(), (unsigned)osc_wifi_has_pass(),
             (unsigned)osc_wifi_ap_mode(), osc_wifi_ap_ssid(), osc_wifi_ip());
    s_server.sendHeader("Cache-Control", "no-store");
    s_server.send(200, "application/json; charset=utf-8", buf);
}

static void handleWifiPost()
{
    if (!s_server.hasArg("ssid"))
    {
        s_server.send(400, "application/json; charset=utf-8",
                      "{\"ok\":0,\"msg\":\"缺少 ssid 参数\"}");
        return;
    }
    String ssid = s_server.arg("ssid");
    String pass = s_server.hasArg("pass") ? s_server.arg("pass") : String("");

    const char *err = "保存失败";
    if (!osc_wifi_save(ssid.c_str(),
                       (pass.length() > 0u) ? pass.c_str() : NULL, &err))
    {
        char buf[128];
        snprintf(buf, sizeof(buf), "{\"ok\":0,\"msg\":\"%s\"}", err);
        s_server.send(400, "application/json; charset=utf-8", buf);
        return;
    }
    s_server.sendHeader("Cache-Control", "no-store");
    s_server.send(200, "application/json; charset=utf-8",
                  "{\"ok\":1,\"msg\":\"已保存，重启后生效\"}");
}

/* ==================================================================
 * init + poll
 * ================================================================== */
void osc_web_init(void)
{
    /* WiFi is already up: osc_wifi_init() ran before this and either joined
       the router (STA) or started the setup AP.  It never reboots on failure,
       so the config page stays reachable to fix a wrong password. */
    Serial.printf("[WEB] serving on %s (SSID \"%s\"%s)\n",
                  osc_wifi_ip(), osc_wifi_ssid(),
                  (osc_wifi_ap_mode() != 0u) ? ", setup AP mode" : "");

    /* mDNS (optional, lets you use oscilloscope.local) */
    MDNS.begin("oscilloscope");
    MDNS.addService("http", "tcp", 80);

    /* WebServer routes */
    s_server.on("/",            HTTP_GET, handleRoot);
    s_server.on("/stat",        HTTP_GET, handleStat);
    s_server.on("/meas",        HTTP_GET, handleMeas);
    s_server.on("/wave",        HTTP_GET, handleWave);
    s_server.on("/csv",         HTTP_GET, handleCsv);
    s_server.on("/cmd",         HTTP_GET, handleCmd);
    s_server.on("/wifi",        HTTP_GET, handleWifiGet);
    s_server.on("/wifi",        HTTP_POST, handleWifiPost);
    s_server.onNotFound([]() {
        s_server.send(404, "text/plain", "not found");
    });
    s_server.begin();
    Serial.println("[WEB] HTTP server started on port 80");

    /* ArduinoOTA */
    ArduinoOTA.onStart([]() {
        Serial.println("[OTA] upload start");
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("\n[OTA] upload done — rebooting");
    });
    ArduinoOTA.onProgress([](unsigned int p, unsigned int t) {
        Serial.printf("[OTA] %u%%\r", (t > 0) ? (p * 100 / t) : 0);
    });
    ArduinoOTA.onError([](ota_error_t e) {
        Serial.printf("[OTA] error %u\n", (unsigned)e);
    });
    ArduinoOTA.begin();
    Serial.println("[OTA] ready (use Arduino IDE/PlatformIO 'Upload OTA')");
}

void osc_web_poll(void)
{
    s_server.handleClient();
    ArduinoOTA.handle();

    /* deferred reboot requested by /cmd?action=reboot|wifireset — the HTTP
       reply has already been sent, so the new WiFi credentials get picked up
       (or the cleared ones get dropped) on the next boot */
    if ((s_reboot_at != 0u) && ((int32_t)(millis() - s_reboot_at) >= 0))
    {
        ESP.restart();
    }
}
