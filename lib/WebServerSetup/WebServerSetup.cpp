#include "WebServerSetup.h"
#include "DebugLogger.h"
#include "ConfigStore.h"
#include <ArduinoJson.h>

WebServerSetup& WebServerSetup::getInstance() {
    static WebServerSetup instance;
    return instance;
}

WebServerSetup::WebServerSetup() : server_(80) {}

void WebServerSetup::init() {
    setupRoutes();
}

void WebServerSetup::start() {
    server_.begin();
    DebugLogger::getInstance().log(LogLevel::INFO, "WebServerSetup started");
}

void WebServerSetup::stop() {
    server_.end();
    DebugLogger::getInstance().log(LogLevel::INFO, "WebServerSetup stopped");
}

void WebServerSetup::setupRoutes() {
    server_.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        static const char HTML[] PROGMEM = R"HTMLEOF(
<!DOCTYPE html><html lang="de">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Salzstand Control – WiFi Setup</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:'Trebuchet MS','Segoe UI',sans-serif;background:radial-gradient(circle at 20% 0%,#0f2140 0%,#0b1630 46%,#070d1f 100%);min-height:100vh;color:#e8f0ff;padding:16px}
.card{background:rgba(9,20,42,.72);border:1px solid rgba(112,146,198,.25);border-radius:16px;padding:20px;max-width:480px;margin:0 auto;box-shadow:0 12px 30px rgba(0,6,20,.45)}
.brand{display:flex;align-items:center;gap:10px;margin-bottom:20px}
.brand-icon{width:40px;height:40px;border-radius:12px;display:inline-flex;align-items:center;justify-content:center;background:linear-gradient(150deg,rgba(98,184,221,.2) 0%,rgba(98,184,221,.05) 100%);border:1px solid rgba(112,146,198,.25);color:#62b8dd;flex-shrink:0}
.brand-icon svg{width:20px;height:20px;fill:currentColor}
h1{font-size:1.35rem;color:#e8f0ff;letter-spacing:.6px;text-transform:uppercase;font-weight:900}
.brand-sub{margin-top:3px;color:#a8bddf;font-size:.8rem;letter-spacing:.16em;text-transform:uppercase}
h2{font-size:1rem;font-weight:700;letter-spacing:.04em;text-transform:uppercase;color:#a8bddf;margin-bottom:16px}
.field-row{display:flex;align-items:center;gap:12px;margin-bottom:12px}
.field-row span{min-width:90px;font-size:.9rem;color:#a8bddf;font-weight:700}
.input-wrap{flex:1;position:relative;display:flex;gap:8px}
input[type=text],input[type=password],select{flex:1;padding:7px 10px;border-radius:8px;border:1px solid rgba(112,146,198,.25);background:rgba(8,18,36,.8);color:#e8f0ff;font-size:.9rem;min-width:0}
input:focus,select:focus{outline:none;border-color:#62b8dd}
.icon-btn{padding:6px 8px;border-radius:8px;border:1px solid rgba(112,146,198,.25);background:rgba(28,52,93,.8);color:#a8bddf;cursor:pointer;display:inline-flex;align-items:center;justify-content:center;flex-shrink:0}
.icon-btn svg{width:16px;height:16px;fill:currentColor}
.icon-btn:hover{background:#2c74bb;color:#fff}
.scan-list{margin-bottom:12px}
.scan-list select{width:100%}
.save-btn{margin-top:8px;width:100%;padding:10px;border-radius:8px;border:none;background:#2c74bb;color:#fff;font-size:.95rem;font-weight:700;letter-spacing:.04em;cursor:pointer}
.save-btn:hover{background:#1a5ea8}
.helper{margin-top:8px;font-size:.8rem;color:#a8bddf;opacity:.7}
#msg{margin-top:12px;font-size:.85rem;padding:8px 10px;border-radius:8px;display:none}
#msg.ok{background:rgba(55,180,100,.18);border:1px solid rgba(55,180,100,.4);color:#76e0a0;display:block}
#msg.err{background:rgba(220,60,60,.18);border:1px solid rgba(220,60,60,.4);color:#f08080;display:block}
</style>
</head>
<body>
<div class="card">
  <div class="brand">
    <span class="brand-icon"><svg viewBox="0 0 24 24"><path d="M6.25 12a1.75 1.75 0 1 1 3.5 0 1.75 1.75 0 0 1-3.5 0Zm5.2 0a.95.95 0 0 1 .95-.95A5.6 5.6 0 0 0 18 5.45a.95.95 0 1 1 1.9 0 7.5 7.5 0 0 1-7.5 7.5.95.95 0 0 1-.95-.95Zm.95 4.55a.95.95 0 0 1 0-1.9A9.2 9.2 0 0 0 21.6 5.45a.95.95 0 1 1 1.9 0c0 6.04-4.91 10.95-10.95 10.95a.95.95 0 0 1-.15 0Zm0-8.95a.95.95 0 1 1 0-1.9 1.7 1.7 0 0 0 1.7-1.7.95.95 0 1 1 1.9 0 3.6 3.6 0 0 1-3.6 3.6Z"/></svg></span>
    <div>
      <h1>Salzstand Control</h1>
      <p class="brand-sub">WiFi Setup</p>
    </div>
  </div>
  <h2>WiFi</h2>
  <div class="field-row">
    <span>SSID:</span>
    <div class="input-wrap">
      <input type="text" id="ssid" name="ssid" autocomplete="off">
      <button class="icon-btn" type="button" onclick="scanWifi()" title="WiFi-Netze suchen">
        <svg viewBox="0 0 24 24"><path d="M10 4a6 6 0 1 0 3.87 10.58l4.27 4.28 1.42-1.42-4.28-4.27A6 6 0 0 0 10 4zm0 2a4 4 0 1 1 0 8 4 4 0 0 1 0-8z"/></svg>
      </button>
    </div>
  </div>
  <div id="scanResult" class="scan-list" style="display:none">
    <div class="field-row">
      <span>Gefundene Netze:</span>
      <select id="netList" onchange="selectNet(this.value)"><option value="">Bitte auswählen</option></select>
    </div>
  </div>
  <div class="field-row">
    <span>Passwort:</span>
    <div class="input-wrap">
      <input type="password" id="pw" name="password" autocomplete="new-password">
      <button class="icon-btn" type="button" onclick="togglePw()" title="Passwort anzeigen">
        <svg viewBox="0 0 24 24"><path d="M12 4.5C7 4.5 2.73 7.61 1 12c1.73 4.39 6 7.5 11 7.5s9.27-3.11 11-7.5c-1.73-4.39-6-7.5-11-7.5zm0 12.5a5 5 0 1 1 0-10 5 5 0 0 1 0 10zm0-8a3 3 0 1 0 0 6 3 3 0 0 0 0-6z"/></svg>
      </button>
    </div>
  </div>
  <button class="save-btn" onclick="saveConfig()">Speichern &amp; Neustart</button>
  <p class="helper">Nach dem Speichern startet der ESP neu und verbindet sich mit dem angegebenen Netzwerk.</p>
  <div id="msg"></div>
</div>
<script>
function togglePw(){
  var i=document.getElementById('pw');
  i.type=i.type==='password'?'text':'password';
}
function selectNet(v){if(v)document.getElementById('ssid').value=v;}
function scanWifi(){
  var btn=event.target.closest('button');
  btn.disabled=true;
  fetch('/scan',{method:'POST'}).then(r=>r.json()).then(nets=>{
    var sel=document.getElementById('netList');
    sel.innerHTML='<option value="">Bitte auswählen</option>';
    nets.forEach(function(n){var o=document.createElement('option');o.value=n.ssid;o.textContent=n.ssid+' ('+n.rssi+' dBm)';sel.appendChild(o);});
    document.getElementById('scanResult').style.display=nets.length?'block':'none';
  }).catch(function(){}).finally(function(){btn.disabled=false;});
}
function saveConfig(){
  var ssid=document.getElementById('ssid').value.trim();
  var pw=document.getElementById('pw').value;
  if(!ssid){showMsg('err','Bitte eine SSID eingeben.');return;}
  var fd=new FormData();fd.append('ssid',ssid);fd.append('password',pw);
  showMsg('ok','Speichere …');
  fetch('/save',{method:'POST',body:fd}).then(r=>r.text()).then(function(){
    showMsg('ok','Gespeichert. ESP startet neu …');
  }).catch(function(){showMsg('err','Fehler beim Speichern.');});
}
function showMsg(cls,txt){
  var el=document.getElementById('msg');
  el.className=cls;el.textContent=txt;
}
</script>
</body></html>
)HTMLEOF";
        request->send_P(200, "text/html", HTML);
    });

    server_.on("/scan", HTTP_POST, [](AsyncWebServerRequest *request) {
        auto networks = WifiManager::getInstance().scanNetworks();
        StaticJsonDocument<2048> doc;
        JsonArray arr = doc.to<JsonArray>();
        for (auto& n : networks) {
            JsonObject obj = arr.createNestedObject();
            obj["ssid"] = n.ssid;
            obj["rssi"] = n.rssi;
        }
        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json);
    });

    server_.on("/save", HTTP_POST, [](AsyncWebServerRequest *request) {
        std::string ssid = request->arg("ssid").c_str();
        std::string password = request->arg("password").c_str();
        WifiConfig config{ssid, password};
        WifiManager::getInstance().setConfig(config);
        request->send(200, "text/plain", "OK");
        delay(1000);
        ESP.restart();
    });
}