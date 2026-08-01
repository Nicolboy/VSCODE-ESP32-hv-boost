#include "web_ui.h"

#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>

#include "config.h"
#include "tms_link.h"

namespace WebUi {

namespace {

WebServer server(80);

const char INDEX_HTML[] PROGMEM = R"html(<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>AlimHT</title>
<style>
body{font-family:monospace;background:#111;color:#eee;padding:1em}
h1{color:#0f0;margin-bottom:0.2em}
table{border-collapse:collapse;margin-top:1em}
td{padding:2px 10px}
.on{color:#0f0}.off{color:#f55}
button{padding:10px 18px;margin:4px 4px 4px 0;font-size:1em;cursor:pointer}
button.on{background:#0f0;color:#111;border:none}
</style></head>
<body>
<h1>AlimHT</h1>
<p>Liaison TMS320 : <span id="link">?</span></p>
<div>
<button id="b_ht" onclick="cmd('ht')">HT</button>
<button id="b_pwm1" onclick="cmd('pwm1')">PWM1</button>
<button id="b_pwm2" onclick="cmd('pwm2')">PWM2</button>
</div>
<table id="tel"></table>
<script>
let state={ht:false,pwm1:false,pwm2:false};
function send(){
  fetch('/api/command',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body:'ht='+(state.ht?1:0)+'&pwm1='+(state.pwm1?1:0)+'&pwm2='+(state.pwm2?1:0)});
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
    document.getElementById('tel').innerHTML =
      '<tr><td>Vin</td><td>'+d.vin.toFixed(1)+' V</td><td>Iin</td><td>'+d.iin.toFixed(2)+' A</td></tr>'+
      '<tr><td>Iout</td><td>'+d.iout.toFixed(2)+' A</td></tr>'+
      '<tr><td>Etage 1</td><td colspan="3">V='+d.v1.toFixed(1)+'V I='+d.i1.toFixed(2)+'A T='+d.t1.toFixed(1)+'C</td></tr>'+
      '<tr><td>Etage 2</td><td colspan="3">V='+d.vout.toFixed(1)+'V I='+d.i2.toFixed(2)+'A T='+d.t2.toFixed(1)+'C</td></tr>';
  });
}
setInterval(refresh, 1000);
refresh();
</script>
</body></html>
)html";

void handleRoot() { server.send_P(200, "text/html", INDEX_HTML); }

void handleState() {
  const Telemetry &t = TmsLink::telemetry();
  const CommandState &cmd = TmsLink::command();
  char buf[400];
  snprintf(buf, sizeof(buf),
           "{\"link\":%s,\"vin\":%.2f,\"iin\":%.2f,\"iout\":%.2f,"
           "\"v1\":%.2f,\"i1\":%.2f,\"t1\":%.2f,"
           "\"vout\":%.2f,\"i2\":%.2f,\"t2\":%.2f,"
           "\"ht\":%s,\"pwm1\":%s,\"pwm2\":%s}",
           TmsLink::linkOk() ? "true" : "false", t.vin, t.iin, t.iout, t.v1, t.i1, t.t1,
           t.vout, t.i2, t.t2, cmd.ht ? "true" : "false", cmd.pwm1 ? "true" : "false",
           cmd.pwm2 ? "true" : "false");
  server.send(200, "application/json", buf);
}

void handleCommand() {
  CommandState cmd;
  cmd.ht = server.arg("ht") == "1";
  cmd.pwm1 = server.arg("pwm1") == "1";
  cmd.pwm2 = server.arg("pwm2") == "1";
  TmsLink::setCommand(cmd);
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
  server.begin();
}

void poll() { server.handleClient(); }

}  // namespace WebUi
