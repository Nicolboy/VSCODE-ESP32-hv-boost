#include "web_ui.h"

#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>

#include "config.h"
#include "oled_ui.h"
#include "tms_link.h"

namespace WebUi {

namespace {

WebServer server(80);

uint32_t g_lastActivityMs = 0;
void markActivity() { g_lastActivityMs = millis(); }

const char INDEX_HTML[] PROGMEM = R"html(<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>AlimHT</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
:root{
  --surface:#1a1a19; --page:#0d0d0d;
  --ink:#ffffff; --ink2:#c3c2b7; --muted:#898781; --track:#2c2c2a;
  --good:#0ca30c; --warning:#fab219; --serious:#ec835a; --critical:#d03b3b;
}
*{box-sizing:border-box}
body{font-family:system-ui,-apple-system,"Segoe UI",sans-serif;background:var(--page);color:var(--ink);padding:1em;margin:0}
h1{color:var(--ink);margin:0 0 0.1em;font-size:1.3em}
.sub{color:var(--ink2);margin:0 0 1em;font-size:0.9em}
.buttons{margin-bottom:1em}
button{padding:10px 18px;margin:0 8px 8px 0;font-size:1em;cursor:pointer;border:1px solid var(--muted);border-radius:6px;background:var(--surface);color:var(--ink)}
button.on{background:var(--good);border-color:var(--good);color:#08210a}
.gauges{display:grid;grid-template-columns:repeat(auto-fit,minmax(64px,1fr));gap:10px;max-width:560px}
.gauge{background:var(--surface);border-radius:8px;padding:10px 4px 8px;display:flex;flex-direction:column;align-items:center;text-align:center}
.gauge-label{color:var(--ink2);font-size:0.8em}
.gauge-value{color:var(--ink);font-weight:600;font-size:0.85em;font-variant-numeric:tabular-nums;margin:2px 0 6px}
.gauge-tick{color:var(--muted);font-size:0.65em}
.gauge-track{position:relative;width:16px;height:120px;border-radius:8px;background:var(--track);overflow:hidden;margin:2px 0}
.gauge-fill{position:absolute;bottom:0;left:0;width:100%;height:0%;border-radius:8px;transition:height 0.3s}
.etages{color:var(--ink2);font-size:0.9em;margin-top:1em;line-height:1.6}
.brightness{margin-bottom:1em}
.brightness label{display:block;color:var(--ink2);font-size:0.85em;margin-bottom:4px}
.brightness input[type=range]{width:100%;max-width:300px}
</style></head>
<body>
<h1>AlimHT</h1>
<p class="sub">Liaison TMS320 : <span id="link">?</span></p>
<div class="buttons">
<button id="b_ht" onclick="cmd('ht')">HT</button>
<button id="b_pwm1" onclick="cmd('pwm1')">PWM1</button>
<button id="b_pwm2" onclick="cmd('pwm2')">PWM2</button>
</div>
<div class="brightness">
<label for="brightness">Luminosite ecran OLED</label>
<input type="range" id="brightness" min="0" max="255" value="159"
       onmousedown="brightnessBusy=true" ontouchstart="brightnessBusy=true"
       onchange="brightnessBusy=false;setBrightness(this.value)">
</div>
<div class="gauges" id="gauges"></div>
<div class="etages" id="etages"></div>
<script>
let state={ht:false,pwm1:false,pwm2:false};

// Plages physiques (voir doc/ESP32-UART.md, coefficients de calibration ADC).
const GAUGES=[
  {key:'vin',  label:'Vin',  unit:'V',  min:0, max:30,  dec:1},
  {key:'iin',  label:'Iin',  unit:'A',  min:0, max:3,   dec:2},
  {key:'v1',   label:'V1 (inter)', unit:'V', min:0, max:50, dec:1},
  {key:'i1',   label:'I1',  unit:'A',  min:0, max:3,   dec:2},
  {key:'vout', label:'Vout', unit:'V', min:0, max:450, dec:0},
  {key:'i2',   label:'I2',  unit:'A',  min:0, max:3,   dec:2},
  {key:'iout', label:'Iout', unit:'mA', min:0, max:50, dec:1, scale:1000},
];

function buildGauges(){
  const root=document.getElementById('gauges');
  root.innerHTML=GAUGES.map(g=>
    '<div class="gauge">'+
    '<div class="gauge-label">'+g.label+'</div>'+
    '<div class="gauge-value" id="v_'+g.key+'">--</div>'+
    '<div class="gauge-tick">'+g.max+'</div>'+
    '<div class="gauge-track"><div class="gauge-fill" id="f_'+g.key+'"></div></div>'+
    '<div class="gauge-tick">'+g.min+' '+g.unit+'</div></div>'
  ).join('');
}

// Vert < 50%, jaune 50-75%, orange 75-90%, rouge 90-100%.
function severityColor(frac){
  if (frac>=0.90) return 'var(--critical)';
  if (frac>=0.75) return 'var(--serious)';
  if (frac>=0.50) return 'var(--warning)';
  return 'var(--good)';
}

function updateGauges(d){
  for (const g of GAUGES){
    const raw=d[g.key]*(g.scale||1);
    const frac=Math.max(0,Math.min(1,(raw-g.min)/(g.max-g.min)));
    document.getElementById('v_'+g.key).textContent=raw.toFixed(g.dec)+' '+g.unit;
    const fill=document.getElementById('f_'+g.key);
    fill.style.height=(frac*100)+'%';
    fill.style.background=severityColor(frac);
  }
}

function send(){
  fetch('/api/command',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body:'ht='+(state.ht?1:0)+'&pwm1='+(state.pwm1?1:0)+'&pwm2='+(state.pwm2?1:0)});
}

let brightnessBusy=false;
function setBrightness(v){
  fetch('/api/brightness',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body:'value='+v});
}
function cmd(k){
  state[k]=!state[k];
  document.getElementById('b_'+k).className = state[k] ? 'on' : '';
  send();
}
function refresh(){
  fetch('/api/state').then(r=>r.json()).then(d=>{
    document.getElementById('link').textContent = d.link ? 'OK' : 'PERDU';
    state = {ht:d.ht, pwm1:d.pwm1, pwm2:d.pwm2};
    for (const k of ['ht','pwm1','pwm2']) {
      document.getElementById('b_'+k).className = state[k] ? 'on' : '';
    }
    updateGauges(d);
    document.getElementById('etages').innerHTML =
      'Etage 1 : T='+d.t1.toFixed(1)+'&deg;C &nbsp; Etage 2 : T='+d.t2.toFixed(1)+'&deg;C';
    if (!brightnessBusy) document.getElementById('brightness').value = d.brightness;
  });
}
buildGauges();
setInterval(refresh, 1000);
refresh();
</script>
</body></html>
)html";

void handleRoot() {
  markActivity();
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleState() {
  markActivity();
  const Telemetry &t = TmsLink::telemetry();
  const CommandState &cmd = TmsLink::command();
  char buf[400];
  snprintf(buf, sizeof(buf),
           "{\"link\":%s,\"vin\":%.2f,\"iin\":%.2f,\"iout\":%.2f,"
           "\"v1\":%.2f,\"i1\":%.2f,\"t1\":%.2f,"
           "\"vout\":%.2f,\"i2\":%.2f,\"t2\":%.2f,"
           "\"ht\":%s,\"pwm1\":%s,\"pwm2\":%s,\"brightness\":%u,\"fault\":%u}",
           TmsLink::linkOk() ? "true" : "false", t.vin, t.iin, t.iout, t.v1, t.i1, t.t1,
           t.vout, t.i2, t.t2, cmd.ht ? "true" : "false", cmd.pwm1 ? "true" : "false",
           cmd.pwm2 ? "true" : "false", OledUi::brightness(), t.fault);
  server.send(200, "application/json", buf);
}

void handleCommand() {
  markActivity();
  CommandState cmd;
  cmd.ht = server.arg("ht") == "1";
  cmd.pwm1 = server.arg("pwm1") == "1";
  cmd.pwm2 = server.arg("pwm2") == "1";
  TmsLink::setCommand(cmd);
  server.send(200, "text/plain", "OK");
}

void handleBrightness() {
  markActivity();
  int v = server.arg("value").toInt();
  if (v < 0) v = 0;
  if (v > 255) v = 255;
  OledUi::setBrightness((uint8_t)v);
  server.send(200, "text/plain", "OK");
}

}  // namespace

void begin() {
  // Pic de courant limite au demarrage radio : reduit le risque de
  // brownout sur une alimentation USB limite.
  WiFi.mode(WIFI_AP);
  WiFi.setTxPower(WIFI_POWER_2dBm);
  WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/state", HTTP_GET, handleState);
  server.on("/api/command", HTTP_POST, handleCommand);
  server.on("/api/brightness", HTTP_POST, handleBrightness);
  server.begin();
}

void poll() { server.handleClient(); }

uint32_t lastActivityMs() { return g_lastActivityMs; }

}  // namespace WebUi
