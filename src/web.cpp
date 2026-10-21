#include <Arduino.h>
#include <ElegantOTA.h>
#include <WebServer.h>
#include <WiFi.h>

#include "config.h"
#include "io_config.h"
#include "humidity_control.h"
#include "rain_control.h"
#include "secrets.h"
#include "sensors.h"
#include "valve_control.h"
#include "web.h"

// ═════════════════════════════════════════════════════════════════════════════
//  WEB SERVER
//
//  Serves the dashboard, a small JSON API and the ElegantOTA firmware updater.
//  Everything here is HTTP on port 80; the UDP-based ArduinoOTA and the WiFi
//  association live in wifi_network.cpp.
//
//  Routes
//    GET  /                     dashboard
//    GET  /api/state            relays + sensor readings as JSON
//    POST /api/control          switch one relay
//    POST /api/rain             set the daily rain start time and duration
//    GET  /api/rs485            bus probe            (commissioning)
//    GET  /api/rs485/scan       baud/address sweep   (commissioning)
//    POST /api/rs485/setaddr    readdress a probe    (commissioning)
//    GET  /update               ElegantOTA
// ═════════════════════════════════════════════════════════════════════════════

static WebServer server(HTTP_PORT);

// ── Dashboard ────────────────────────────────────────────────────────────────
// Held in flash as a raw string rather than on a filesystem: it is small, and
// keeping it in the binary means an OTA update can never leave the UI and the
// firmware out of step.
static const char INDEX_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Garden Valve Control</title>
<style>
body{font-family:system-ui,sans-serif;background:#111;color:#eee;margin:0;padding:1.5rem;max-width:34rem}
h1{font-size:1.25rem;margin:0 0 1rem}
h2{font-size:.8rem;text-transform:uppercase;letter-spacing:.08em;color:#888;margin:1.5rem 0 .25rem}
.row{display:flex;align-items:center;gap:.75rem;padding:.6rem 0;border-bottom:1px solid #2a2a2a}
.dot{width:.9rem;height:.9rem;border-radius:50%;background:#444;flex:none}
.on{background:#3c3;box-shadow:0 0 .5rem #3c3}
.bad{background:#c33}
.name{flex:1}
.val{font-variant-numeric:tabular-nums;color:#bbb}
.val.open{color:#5d5;font-weight:600}
.val.shut{color:#777}
button{background:#2a2a2a;color:#eee;border:1px solid #555;border-radius:.3rem;padding:.4rem .9rem;cursor:pointer}
button:hover{filter:brightness(1.25)}
button.open{background:#14361f;border-color:#2e9e4b;color:#7ede97}
button.close{background:#4a1a1a;border-color:#c33;color:#f59a9a}
.seg{display:flex;border:1px solid #555;border-radius:.4rem;overflow:hidden;width:fit-content}
.seg button{border:0;border-radius:0;background:#1c1c1c;color:#777;font-weight:600;letter-spacing:.06em;padding:.55rem 1.5rem}
.seg button.act-man{background:#d8891f;color:#111}
.seg button.act-auto{background:#2e9e4b;color:#fff}
input[type=time],input[type=number]{background:#1c1c1c;color:#eee;border:1px solid #555;border-radius:.3rem;padding:.35rem .5rem;font:inherit;color-scheme:dark;width:7rem;box-sizing:border-box;text-align:right}
input:invalid{border-color:#c33}
button.apply{background:#14361f;border-color:#2e9e4b;color:#7ede97;font-weight:600;letter-spacing:.06em}
button.apply:disabled{background:#1c1c1c;border-color:#333;color:#555;cursor:default;filter:none}
.row .msg{font-size:.8rem;color:#999;flex:1}
#hint{font-size:.8rem;color:#999;margin:.6rem 0 0}
footer{margin-top:1.75rem;font-size:.8rem;color:#888}
a{color:#6af}
</style></head><body>
<h1>Garden Valve Control</h1>
<div class="seg">
  <button id="m-man" onclick="setMode('manual')">MANUAL</button>
  <button id="m-auto" onclick="setMode('automatic')">AUTOMATIC</button>
</div>
<p id="hint"></p>
<h2>Valves</h2>
<div id="outputs">loading...</div>
<h2>Rain schedule</h2>
<div class="row"><div class="name">Start time (daily)</div>
  <input type="time" id="rain-start" required oninput="rainEdited()"></div>
<div class="row"><div class="name">Duration (min, max 60)</div>
  <input type="number" id="rain-dur" min="1" max="60" step="1" required oninput="rainEdited()"></div>
<div class="row"><div class="msg" id="rain-msg"></div>
  <button class="apply" id="rain-apply" onclick="applyRain()" disabled>APPLY</button></div>
<div id="automation" style="display:none">
<h2>Automation</h2>
<div id="auto-info"></div>
</div>
<h2>Sensors</h2>
<div id="sensors"></div>
<footer><span id="fw"></span> &middot; <a href="/update">OTA update</a></footer>
<script>
let rainDirty=false;
function rainEdited(){
  const t=document.getElementById('rain-start'), d=document.getElementById('rain-dur');
  rainDirty=true;
  document.getElementById('rain-msg').textContent='';
  document.getElementById('rain-apply').disabled=!(t.value&&d.checkValidity()&&d.value!=='');
}
async function applyRain(){
  const t=document.getElementById('rain-start').value, d=document.getElementById('rain-dur').value;
  const msg=document.getElementById('rain-msg');
  const r=await fetch('/api/rain?start='+encodeURIComponent(t)+'&duration='+d,{method:'POST'});
  if(r.ok){
    rainDirty=false;
    document.getElementById('rain-apply').disabled=true;
    msg.textContent='Applied';
    refresh();
  } else {
    msg.textContent=(await r.json()).error||'Rejected';
  }
}
async function refresh(){
  const s=await (await fetch('/api/state')).json();
  const manual=(s.mode==='manual');

  // Never overwrite values the user is still editing.
  if(!rainDirty&&s.automation&&s.automation.rain){
    const r=s.automation.rain;
    document.getElementById('rain-start').value=String(r.start_hour).padStart(2,'0')+':'+String(r.start_minute).padStart(2,'0');
    document.getElementById('rain-dur').value=Math.round(r.duration_s/60);
  }

  document.getElementById('fw').textContent=s.firmware+' @ '+s.ip;
  document.getElementById('m-man').className=manual?'act-man':'';
  document.getElementById('m-auto').className=manual?'':'act-auto';
  document.getElementById('hint').textContent=manual
    ? 'Manual override - valves are operated from this page, automation suspended.'
    : 'Automation in control - switch to MANUAL to operate valves by hand.';

  document.getElementById('outputs').innerHTML=s.outputs.map(o=>{
    // Buttons always show the *current* state - CLOSED at rest, OPEN once
    // pressed - never the action a click would perform. always_manual
    // outputs (LIGHT) stay operable even while the rest are automated.
    const label=o.controllable?(o.state?'OPEN':'CLOSED'):(o.state?'ON':'OFF');
    const ctrl=(o.controllable&&(manual||o.always_manual))
      ? `<button class="${o.state?'open':'close'}" onclick="set('${o.name}',${o.state?0:1})">${label}</button>`
      : `<div class="val ${o.state?'open':'shut'}">${label}</div>`;
    // The automated valves also show a compact status next to their row: the
    // mist valve its duty cycle as a run length, the rain valve its daily
    // start time and duration.
    let duty='';
    const mist=s.automation&&s.automation.mist;
    const rain=s.automation&&s.automation.rain;
    if(mist&&o.name===mist.valve){
      const mins=mist.duty*mist.period_s/60;
      const minsText=(mins>0&&mins<1)?mins.toFixed(1):Math.round(mins);
      const emoji=mist.duty<=0?'🌵':mist.duty<=0.25?'💧':mist.duty<=0.5?'💦':mist.duty<=0.75?'🌧':'🌊';
      duty=`<div class="val duty">${minsText}m ${emoji}</div>`;
    }
    if(rain&&o.name===rain.valve){
      const hh=String(rain.start_hour).padStart(2,'0'), mm=String(rain.start_minute).padStart(2,'0');
      duty=`<div class="val duty">${hh}:${mm} · ${Math.round(rain.duration_s/60)}m</div>`;
    }
    return `<div class="row"><div class="dot ${o.state?'on':''}"></div><div class="name">${o.label}</div>${duty}${ctrl}</div>`;
  }).join('');

  document.getElementById('sensors').innerHTML=(s.sensors||[]).map(x=>
    `<div class="row"><div class="dot ${x.ok?'on':'bad'}"></div><div class="name">${x.label}</div>`+
    `<div class="val">${x.ok?x.temperature_c.toFixed(1)+' &deg;C &nbsp; '+x.humidity_pct.toFixed(1)+' %RH'
                        :(x.last_error||'no data')}</div></div>`).join('');

  const a=s.automation;
  document.getElementById('automation').style.display=manual?'none':'block';
  if(a&&!manual){
    const mist=a.mist, rain=a.rain;
    const remain=Math.max(0,mist.period_s-mist.elapsed_s);
    let html=
      `<div class="row"><div class="dot ${mist.valve_on?'on':''}"></div><div class="name">Mist valve</div>`+
      `<div class="val">${mist.valve_on?'MISTING':'idle'}</div></div>`+
      `<div class="row"><div class="name">Target / duty cycle</div>`+
      `<div class="val">${mist.target_pct.toFixed(0)}% RH &nbsp; @ &nbsp; ${(mist.duty*100).toFixed(0)}%</div></div>`+
      `<div class="row"><div class="name">Next cycle in</div><div class="val">${remain}s`+
      `${mist.time_synced?'':' (clock not synced yet)'}</div></div>`;

    const hh=String(rain.start_hour).padStart(2,'0'), mm=String(rain.start_minute).padStart(2,'0');
    const lastRun=rain.has_last_run?hh+':'+mm+' &middot; '+Math.round(rain.last_run_duration_s/60)+'m':'none yet';
    html+=
      `<div class="row"><div class="dot ${rain.valve_on?'on':''}"></div><div class="name">Rain valve</div>`+
      `<div class="val">${rain.valve_on?'RAINING':'idle'}</div></div>`+
      `<div class="row"><div class="name">Daily run</div>`+
      `<div class="val">${hh}:${mm} &nbsp; for &nbsp; ${Math.round(rain.duration_s/60)}m</div></div>`+
      `<div class="row"><div class="name">Last run</div><div class="val">${lastRun}`+
      `${rain.time_synced?'':' (clock not synced yet)'}</div></div>`;

    document.getElementById('auto-info').innerHTML=html;
  }
}
async function set(name,state){
  await fetch('/api/control?name='+name+'&state='+state,{method:'POST'});
  refresh();
}
async function setMode(m){
  await fetch('/api/mode?value='+m,{method:'POST'});
  refresh();
}
refresh();setInterval(refresh,REFRESH_MS);
</script></body></html>
)HTML";

namespace
{
    // ── GET / ────────────────────────────────────────────────────────────────
    void handleRoot()
    {
        // The refresh period lives in config.h, so patch it into the page on
        // the way out rather than duplicating the value in the JavaScript.
        String page = FPSTR(INDEX_HTML);
        page.replace("REFRESH_MS", String(DASHBOARD_REFRESH_MS));
        server.send(200, "text/html", page);
    }

    // ── GET /api/state ───────────────────────────────────────────────────────
    // Full snapshot: firmware, address, relays and sensor readings. Built by
    // string concatenation rather than a JSON library — the payload is small
    // and fixed in shape, so a dependency would not earn its place.
    void handleApiState()
    {
        String json = "{";
        json += "\"firmware\":\"" + String(FIRMWARE_VERSION) + "\",";
        json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
        json += "\"mode\":\"" + String(controlModeName()) + "\",";

        json += "\"outputs\":[";
        for (int i = 0; i < NUM_OUTPUTS; i++)
        {
            if (i)
                json += ",";
            json += "{\"name\":\"" + String(OUTPUTS[i].name) + "\",";
            json += "\"label\":\"" + String(OUTPUTS[i].label) + "\",";
            json += "\"state\":" + String(valveState(i) ? "true" : "false") + ",";
            json += "\"controllable\":" + String(OUTPUTS[i].controllable ? "true" : "false") + ",";
            json += "\"always_manual\":" + String(OUTPUTS[i].alwaysManual ? "true" : "false") + "}";
        }
        json += "],";

        json += "\"sensors\":[";
        for (int i = 0; i < NUM_SENSORS; i++)
        {
            if (i)
                json += ",";
            const SensorReading &reading = sensorReading(i);

            json += "{\"name\":\"" + String(SENSORS[i].name) + "\",";
            json += "\"label\":\"" + String(SENSORS[i].label) + "\",";
            json += "\"address\":" + String(SENSORS[i].address) + ",";
            json += "\"ok\":" + String(reading.valid ? "true" : "false") + ",";

            // Values are omitted entirely when invalid, so a client can never
            // mistake a stale reading for a fresh one.
            if (reading.valid)
            {
                json += "\"temperature_c\":" + String(reading.temperatureC, 1) + ",";
                json += "\"humidity_pct\":" + String(reading.humidityPct, 1) + ",";
            }

            json += "\"errors\":" + String(reading.errors) + ",";
            json += "\"last_error\":\"" + String(reading.lastError) + "\",";
            json += "\"raw\":\"" + String(reading.lastRaw) + "\",";
            json += "\"age_s\":" + String(reading.lastOkMs ? (millis() - reading.lastOkMs) / 1000 : 0) + "}";
        }
        json += "],";

        HumidityControlStatus mist = humidityControlStatus();
        RainControlStatus rain = rainControlStatus();
        json += "\"automation\":{";
        json += "\"mist\":{";
        json += "\"valve\":\"" + String(HUMIDITY_VALVE_NAME) + "\",";
        json += "\"target_pct\":" + String(mist.targetPct, 1) + ",";
        json += "\"duty\":" + String(mist.dutyCycle, 3) + ",";
        json += "\"valve_on\":" + String(mist.valveOn ? "true" : "false") + ",";
        json += "\"period_s\":" + String(mist.periodS) + ",";
        json += "\"elapsed_s\":" + String(mist.elapsedS) + ",";
        json += "\"sensor_valid\":" + String(mist.sensorValid ? "true" : "false") + ",";
        json += "\"time_synced\":" + String(mist.timeSynced ? "true" : "false") + "},";
        json += "\"rain\":{";
        json += "\"valve\":\"" + String(RAIN_VALVE_NAME) + "\",";
        json += "\"valve_on\":" + String(rain.valveOn ? "true" : "false") + ",";
        json += "\"start_hour\":" + String(rain.startHour) + ",";
        json += "\"start_minute\":" + String(rain.startMinute) + ",";
        json += "\"duration_s\":" + String(rain.durationS) + ",";
        json += "\"has_last_run\":" + String(rain.hasLastRun ? "true" : "false") + ",";
        json += "\"last_run_duration_s\":" + String(rain.lastRunDurationS) + ",";
        json += "\"time_synced\":" + String(rain.timeSynced ? "true" : "false") + "}";
        json += "}";

        json += "}";
        server.send(200, "application/json", json);
    }

    // ── POST /api/control?name=<key>&state=<0|1> ─────────────────────────────
    void handleApiControl()
    {
        String name = server.arg("name");
        String stateArg = server.arg("state");

        if (name.isEmpty() || stateArg.isEmpty())
        {
            server.send(400, "application/json",
                        "{\"ok\":false,\"error\":\"Missing name or state\"}");
            return;
        }

        int index = valveIndexByName(name.c_str());
        if (index < 0)
        {
            server.send(404, "application/json",
                        "{\"ok\":false,\"error\":\"Output not found\"}");
            return;
        }
        if (!OUTPUTS[index].controllable)
        {
            server.send(403, "application/json",
                        "{\"ok\":false,\"error\":\"Not controllable\"}");
            return;
        }

        // Refused rather than silently overridden on the next automation pass,
        // which would look like the command was lost. alwaysManual outputs
        // (LIGHT) sit outside the mode group, so they are always allowed.
        if (controlMode() != MODE_MANUAL && !OUTPUTS[index].alwaysManual)
        {
            server.send(409, "application/json",
                        "{\"ok\":false,\"error\":\"Controller is in automatic mode\"}");
            return;
        }

        valveSet(index, stateArg == "1" || stateArg == "true");
        server.send(200, "application/json", "{\"ok\":true}");
    }

    // ── POST /api/mode?value=manual|automatic ─────────────────────────
    void handleApiMode()
    {
        String value = server.arg("value");

        if (value == "manual")
            setControlMode(MODE_MANUAL);
        else if (value == "automatic")
            setControlMode(MODE_AUTOMATIC);
        else
        {
            server.send(400, "application/json",
                        "{\"ok\":false,\"error\":\"value must be manual or automatic\"}");
            return;
        }

        server.send(200, "application/json",
                    "{\"ok\":true,\"mode\":\"" + String(controlModeName()) + "\"}");
    }

    // ── POST /api/rain?start=HH:MM&duration=<minutes> ────────────────────────
    void handleApiRain()
    {
        String start = server.arg("start");
        String durArg = server.arg("duration");

        bool formatOk = start.length() == 5 && start[2] == ':' &&
                        isDigit(start[0]) && isDigit(start[1]) &&
                        isDigit(start[3]) && isDigit(start[4]) && durArg.length() > 0;
        for (unsigned i = 0; formatOk && i < durArg.length(); i++)
            formatOk = isDigit(durArg[i]);

        if (!formatOk || durArg.length() > 3 ||
            !rainControlSetSchedule(start.substring(0, 2).toInt(), start.substring(3).toInt(), durArg.toInt()))
        {
            server.send(400, "application/json",
                        "{\"ok\":false,\"error\":\"start must be HH:MM, duration 1-" +
                            String(RAIN_DURATION_MAX_MIN) + "\"}");
            return;
        }
        server.send(200, "application/json", "{\"ok\":true}");
    }

    // ── GET /api/rs485?addr=N[&loopback=1] ───────────────────────────────────
    // Single blocking transaction, returns the raw bytes. See sensors.cpp.
    void handleApiRs485()
    {
        uint8_t addr = server.arg("addr").isEmpty()
                           ? MODBUS_ADDRESS_MIN
                           : server.arg("addr").toInt();
        bool loopback = server.arg("loopback") == "1";

        String hex = rs485Probe(addr, loopback);
        server.send(200, "application/json",
                    "{\"addr\":" + String(addr) +
                        ",\"loopback\":" + String(loopback ? "true" : "false") +
                        ",\"raw\":\"" + hex + "\"}");
    }

    // ── GET /api/rs485/scan?max=N ────────────────────────────────────────────
    void handleApiRs485Scan()
    {
        uint8_t maxAddr = server.arg("max").isEmpty()
                              ? RS485_SCAN_DEFAULT_MAX_ADDR
                              : server.arg("max").toInt();
        server.send(200, "application/json", rs485Scan(maxAddr));
    }

    // ── POST /api/rs485/setaddr?from=A&to=B ──────────────────────────────────
    // Only meaningful with a single probe on the bus: the write reaches every
    // device answering at `from`, so otherwise they would all take the new
    // address and collide again.
    void handleApiRs485SetAddr()
    {
        uint8_t from = server.arg("from").toInt();
        uint8_t to = server.arg("to").toInt();

        if (from < MODBUS_ADDRESS_MIN || from > MODBUS_ADDRESS_MAX ||
            to < MODBUS_ADDRESS_MIN || to > MODBUS_ADDRESS_MAX)
        {
            server.send(400, "application/json",
                        "{\"ok\":false,\"error\":\"from/to out of range\"}");
            return;
        }

        String hex = rs485SetAddress(from, to);
        Serial.printf("[485] set address %u -> %u [%s]\n", from, to, hex.c_str());
        server.send(200, "application/json",
                    "{\"from\":" + String(from) + ",\"to\":" + String(to) +
                        ",\"raw\":\"" + hex + "\"}");
    }
} // namespace

void webSetup()
{
    server.on("/", HTTP_GET, handleRoot);
    server.on("/api/state", HTTP_GET, handleApiState);
    server.on("/api/control", HTTP_POST, handleApiControl);
    server.on("/api/mode", HTTP_POST, handleApiMode);
    server.on("/api/rain", HTTP_POST, handleApiRain);
    server.on("/api/rs485", HTTP_GET, handleApiRs485);
    server.on("/api/rs485/scan", HTTP_GET, handleApiRs485Scan);
    server.on("/api/rs485/setaddr", HTTP_POST, handleApiRs485SetAddr);

    ElegantOTA.setAuth(OTA_HTTP_USER, OTA_HTTP_PASS);
    ElegantOTA.begin(&server);
    server.begin();

    String ip = WiFi.localIP().toString();
    Serial.println("[WEB] firmware " + String(FIRMWARE_VERSION) + " ready");
    Serial.println("[WEB]   dashboard : http://" + ip + "/");
    Serial.println("[WEB]   state     : http://" + ip + "/api/state");
    Serial.println("[WEB]   OTA       : http://" + ip + "/update");
}

void webLoop()
{
    server.handleClient();
    ElegantOTA.loop();
}
