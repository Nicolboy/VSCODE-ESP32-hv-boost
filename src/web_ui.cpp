#include "web_ui.h"

#include <Arduino.h>
#include <ArduinoJson.h>
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
button:disabled{opacity:0.35;cursor:not-allowed}
.voset-offset{display:inline-block;min-width:3.5em;text-align:center;color:var(--ink2)}
.gauges{display:grid;grid-template-columns:repeat(auto-fit,minmax(64px,1fr));gap:10px;max-width:560px}
.gauge{background:var(--surface);border-radius:8px;padding:10px 4px 8px;display:flex;flex-direction:column;align-items:center;text-align:center}
.gauge-label{color:var(--ink2);font-size:0.8em}
.gauge-value{color:var(--ink);font-weight:600;font-size:0.85em;font-variant-numeric:tabular-nums;margin:2px 0 6px}
.gauge-tick{color:var(--muted);font-size:0.65em}
.gauge-track{position:relative;width:16px;height:120px;border-radius:8px;background:var(--track);overflow:hidden;margin:2px 0}
.gauge-fill{position:absolute;bottom:0;left:0;width:100%;height:0%;border-radius:8px;transition:height 0.3s}
.etages{color:var(--ink2);font-size:0.9em;margin-top:1em;line-height:1.6}
.fault-active{color:var(--critical);font-weight:700}
.brightness{margin-bottom:1em}
.brightness label{display:block;color:var(--ink2);font-size:0.85em;margin-bottom:4px}
.brightness input[type=range]{width:100%;max-width:300px}
</style></head>
<body>
<h1>AlimHT</h1>
<p class="sub">Liaison TMS320 : <span id="link">?</span></p>
<p class="sub">Etat : <span id="fault">?</span></p>
<p class="sub">Consignes rejetees (REJ) : <span id="rej">0</span></p>
<p class="sub">Consigne V1 appliquee (V1SP) : <span id="v1sp">--</span> &nbsp; Consigne etage 2 appliquee (VOSP) : <span id="vosp">--</span></p>
<div class="buttons">
<button id="b_run" onclick="cmd('run')">Marche</button>
<button id="b_ht" onclick="cmd('ht')">HT</button>
</div>
<div class="buttons">
<!-- Etage 2 (VOSET) : palier + decalage fin, voir doc/claudeesp32-etage2-interface.md §3.3.
     Paliers actifs seulement a l'arret (RUN=0) -- change de mode = refuse en
     marche cote TMS320 (REJ++). Decalage +/-10V actif en marche (rampe cote
     TMS320), desactive en mode OFF (VOSET doit rester exactement 0). -->
<button id="b_vo_0" onclick="setPalier(0)">OFF</button>
<button id="b_vo_200" onclick="setPalier(200)">200V</button>
<button id="b_vo_300" onclick="setPalier(300)">300V</button>
<button id="b_vo_400" onclick="setPalier(400)">400V</button>
<button id="b_vo_minus" onclick="adjOffset(-10)">-10V</button>
<span class="voset-offset" id="voset_offset">+0V</span>
<button id="b_vo_plus" onclick="adjOffset(10)">+10V</button>
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
// RUN et HT sont deux commandes independantes cote TMS320 (voir
// doc/esp32_agent.md) : RUN arme le chemin de puissance (regulation), HT
// n'autorise HV_EN qu'ensuite, une fois RUN actif et l'etage 2 etabli.
// palier/offset : decomposition du VOSET emis (voir doc/claudeesp32-etage2-
// interface.md §3.3) -- VOSET envoye = palier + offset, arithmetique entiere.
let state={ht:false,run:false,palier:0,offset:0};

// Libelles FAULT (voir enum TmsFault, include/protocol.h) -- index = code.
const FAULT_LABELS=['OK','EMUSTOP','OVERI1','OVERI2','OVERT1','OVERT2','OVERV1','OVERVOUT','LINKLOST','UNDERVIN'];
// Codes transitoires (se relevent seuls) : ne jamais basculer sur "KO" au
// timeout de liaison tant que c'est la derniere valeur connue -- voir
// isFaultLatched() cote firmware (include/protocol.h).
const FAULT_TRANSIENT=[1,8];

// Plages d'affichage (voir doc/ESP32-UART.md) -- alignees sur les bornes
// des bargraphs OLED (src/oled_ui.cpp), pas la pleine echelle ADC.
const GAUGES=[
  {key:'vin',  label:'Vin',  unit:'V',  min:0,   max:25,  dec:1},
  {key:'iin',  label:'Iin',  unit:'A',  min:0,   max:3,   dec:2},
  {key:'v1',   label:'V1 (inter)', unit:'V', min:0, max:50, dec:1},
  {key:'i1',   label:'I1',  unit:'A',  min:0,   max:3,   dec:2},
  {key:'vout', label:'Vout', unit:'V', min:200, max:500, dec:0},
  {key:'i2',   label:'I2',  unit:'A',  min:0,   max:3,   dec:2},
  {key:'iout', label:'Iout', unit:'mA', min:0,  max:50,  dec:1, scale:1000},
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
    body:'ht='+(state.ht?1:0)+'&run='+(state.run?1:0)+'&voutset='+(state.palier+state.offset)});
}

let brightnessBusy=false;
function setBrightness(v){
  fetch('/api/brightness',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body:'value='+v});
}
function cmd(k){
  state[k]=!state[k];
  document.getElementById('b_'+k).className = state[k] ? 'on' : '';
  updateVosetUI();
  send();
}

// Boutons de palier actifs seulement a l'arret : basculer VOSET de/vers 0
// en marche est refuse par le TMS320 (REJ++). setPalier() n'est de toute
// facon jamais appelable pendant la marche (bouton disabled), mais le garde
// evite un envoi errone si l'etat du bouton n'a pas encore ete resynchronise.
function setPalier(p){
  if (state.run) return;
  state.palier = p;
  state.offset = 0;
  updateVosetUI();
  send();
}

// Decalage fin (0 a +100V par pas de 10), actif en marche (rampe cote
// TMS320) -- mais pas en mode OFF, ou VOSET doit rester exactement 0.
function adjOffset(delta){
  if (state.palier === 0) return;
  state.offset = Math.max(0, Math.min(100, state.offset + delta));
  updateVosetUI();
  send();
}

function updateVosetUI(){
  const canChangePalier = !state.run;
  for (const p of [0,200,300,400]) {
    const btn = document.getElementById('b_vo_'+p);
    btn.disabled = !canChangePalier;
    btn.className = (state.palier === p) ? 'on' : '';
  }
  const canOffset = state.palier !== 0;
  document.getElementById('b_vo_minus').disabled = !canOffset;
  document.getElementById('b_vo_plus').disabled = !canOffset;
  document.getElementById('voset_offset').textContent = '+'+state.offset+'V';
}

// Reconstruit palier/offset a partir de VOSP recu (verite TMS320) --
// paliers volontairement en recouvrement (doc/claudeesp32-etage2-interface.md
// §3.3), la decomposition n'est donc pas toujours unique ; on prend le
// premier palier dont la plage [p, p+100] contient VOSP.
function syncVoset(voutSp){
  if (voutSp === 0) { state.palier = 0; state.offset = 0; return; }
  for (const p of [200,300,400]) {
    const off = Math.round(voutSp - p);
    if (off >= 0 && off <= 100) { state.palier = p; state.offset = off; return; }
  }
}
function refresh(){
  fetch('/api/state').then(r=>r.json()).then(d=>{
    document.getElementById('link').textContent = d.link ? 'OK' : 'PERDU';
    // "KO" seulement si liaison perdue ET dernier defaut connu n'est pas
    // transitoire (1=EMUSTOP, 8=liaison perdue cote TMS320) : ces deux
    // codes arretent aussi l'UART cote TMS320 (attendu, pas une panne) --
    // on continue d'afficher le code (derniere valeur connue) le temps
    // que le TMS320 reparte, plutot que "KO".
    const faultEl = document.getElementById('fault');
    const showKo = !d.link && !FAULT_TRANSIENT.includes(d.fault);
    faultEl.textContent = showKo ? 'KO' : (FAULT_LABELS[d.fault] || ('CODE '+d.fault));
    faultEl.className = (showKo || d.fault) ? 'fault-active' : '';
    const rejEl = document.getElementById('rej');
    rejEl.textContent = d.rej;
    rejEl.className = d.rej > 0 ? 'fault-active' : '';
    document.getElementById('v1sp').textContent = d.v1_sp.toFixed(1)+' V';
    document.getElementById('vosp').textContent = d.vout_sp.toFixed(0)+' V';
    state.ht = d.ht;
    state.run = d.run;
    document.getElementById('b_ht').className = state.ht ? 'on' : '';
    document.getElementById('b_run').className = state.run ? 'on' : '';
    syncVoset(d.vout_sp);
    updateVosetUI();
    updateGauges(d);
    // vout_sp==0 = mode etage 1 seul (convention TMS320, pas une panne) --
    // voir doc/tms320_agent.md.
    const stage1Only = d.vout_sp === 0;
    document.getElementById('etages').innerHTML =
      (stage1Only ? '<strong>Etage 1 seul (etage 2 desactive)</strong><br>' : '') +
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
  char buf[420];
  snprintf(buf, sizeof(buf),
           "{\"link\":%s,\"vin\":%.2f,\"iin\":%.2f,\"iout\":%.2f,"
           "\"v1\":%.2f,\"i1\":%.2f,\"t1\":%.2f,"
           "\"vout\":%.2f,\"i2\":%.2f,\"t2\":%.2f,"
           "\"ht\":%s,\"run\":%s,\"brightness\":%u,\"fault\":%u,"
           "\"v1_sp\":%.2f,\"vout_sp\":%.2f,\"rej\":%u}",
           TmsLink::linkOk() ? "true" : "false", t.vin, t.iin, t.iout, t.v1, t.i1, t.t1,
           t.vout, t.i2, t.t2, cmd.ht ? "true" : "false", cmd.run ? "true" : "false",
           OledUi::brightness(), t.fault, t.v1Sp, t.voutSp, t.rej);
  server.send(200, "application/json", buf);
}

void handleCommand() {
  markActivity();
  // Dashboard simple : pilote HT, RUN et VOSET (palier+decalage compose
  // cote JS, voir INDEX_HTML). v1Set n'est jamais touche ici : fixe a 50.0
  // par decision de conception (voir protocol.h, CommandState::v1Set) --
  // seul /api/setpoint (orchestration externe) peut le faire varier.
  // Le sequencement RUN=0 -> VOSET -> RUN=1 est impose cote IHM (boutons
  // de palier desactives pendant la marche, voir setPalier() en JS) ;
  // l'ESP32 ne le revalide pas ici, seul le TMS320 en est juge.
  CommandState cmd = TmsLink::command();
  cmd.ht = server.arg("ht") == "1";
  cmd.run = server.arg("run") == "1";
  if (server.hasArg("voutset")) cmd.voutSet = server.arg("voutset").toFloat();
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

// ---------------------------------------------------------------------
// API HTTP d'orchestration externe (doc/orchestration.md, doc/esp32_agent.md)
// -- distincte du dashboard ci-dessus (destine a un humain), destinee a un
// agent/orchestrateur externe (JSON, champs de controle STATE/V1SP/VOSP/
// REJ inclus). L'ESP32 relaie fidelement les trames $C/$T, il ne valide
// ni ne clippe les bornes physiques -- seul le TMS320 en est juge.
// ---------------------------------------------------------------------

void serializeTelemetry(JsonObject obj) {
  const Telemetry &t = TmsLink::telemetry();
  obj["freq1"] = t.freq1;
  obj["freq2"] = t.freq2;
  obj["duty1"] = t.duty1;
  obj["duty2"] = t.duty2;
  obj["vin"] = t.vin;
  obj["iin"] = t.iin;
  obj["v1"] = t.v1;
  obj["i1"] = t.i1;
  obj["t1"] = t.t1;
  obj["vout"] = t.vout;
  obj["i2"] = t.i2;
  obj["t2"] = t.t2;
  obj["iout"] = t.iout;
  obj["fault"] = t.fault;
  obj["state"] = t.state;
  obj["v1_sp"] = t.v1Sp;
  obj["vout_sp"] = t.voutSp;
  obj["rej"] = t.rej;
  obj["link_ok"] = TmsLink::linkOk();
  obj["age_ms"] = millis() - t.lastUpdateMs;
}

void handleTelemetry() {
  markActivity();
  JsonDocument doc;
  serializeTelemetry(doc.to<JsonObject>());
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

// Fusion partielle : part de l'etat de commande connu (TmsLink::command())
// et n'ecrase que les champs presents dans le corps JSON -- un champ
// absent ne doit rien changer (meme convention que les tags $C optionnels
// cote TMS320). Repond la trame $C effectivement envoyee et un releve de
// telemetrie juste apres, pour verifier "rej" sans requete supplementaire.
void handleSetpoint() {
  markActivity();
  JsonDocument req;
  DeserializationError err = deserializeJson(req, server.arg("plain"));

  CommandState cmd = TmsLink::command();
  if (!err) {
    if (!req["ht"].isNull()) cmd.ht = req["ht"].as<bool>();
    if (!req["v1_set"].isNull()) cmd.v1Set = req["v1_set"].as<float>();
    if (!req["vout_set"].isNull()) cmd.voutSet = req["vout_set"].as<float>();
    if (!req["run"].isNull()) cmd.run = req["run"].as<bool>();
  }
  TmsLink::setCommand(cmd);

  JsonDocument resp;
  resp["frame_sent"] = TmsLink::lastFrameSent();
  serializeTelemetry(resp["telemetry_after"].to<JsonObject>());
  String out;
  serializeJson(resp, out);
  server.send(200, "application/json", out);
}

// Arret d'urgence logiciel : RUN=0, HT=0. Ne depend d'aucun parsing de
// corps JSON (contrairement a handleSetpoint()) pour rester disponible
// meme si une requete precedente etait malformee.
void handleStop() {
  markActivity();
  CommandState cmd = TmsLink::command();
  cmd.run = false;
  cmd.ht = false;
  TmsLink::setCommand(cmd);

  JsonDocument resp;
  resp["frame_sent"] = TmsLink::lastFrameSent();
  serializeTelemetry(resp["telemetry_after"].to<JsonObject>());
  String out;
  serializeJson(resp, out);
  server.send(200, "application/json", out);
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
  // API d'orchestration externe (doc/orchestration.md).
  server.on("/api/telemetry", HTTP_GET, handleTelemetry);
  server.on("/api/setpoint", HTTP_POST, handleSetpoint);
  server.on("/api/stop", HTTP_POST, handleStop);
  server.begin();
}

void poll() { server.handleClient(); }

uint32_t lastActivityMs() { return g_lastActivityMs; }

}  // namespace WebUi
