#include <Arduino.h>
#include <ElegantOTA.h>
#include <WebServer.h>
#include <WiFi.h>

#include "config.h"
#include "io_config.h"
#include "humidity_control.h"
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
<div id="automation" style="display:none">
<h2>Automation</h2>
<div id="auto-info"></div>
</div>
<h2>Sensors</h2>
<div id="sensors"></div>
<footer><span id="fw"></span> &middot; <a href="/update">OTA update</a></footer>
<script>
async function refresh(){
  const s=await (await fetch('/api/state')).json();
  const manual=(s.mode==='manual');

  document.getElementById('fw').textContent=s.firmware+' @ '+s.ip;
  document.getElementById('m-man').className=manual?'act-man':'';
  document.getElementById('m-auto').className=manual?'':'act-auto';
  document.getElementById('hint').textContent=manual
    ? 'Manual override - valves are operated from this page, automation suspended.'
    : 'Automation in control - switch to MANUAL to operate valves by hand.';

  document.getElementById('outputs').innerHTML=s.outputs.map(o=>{
    // Buttons always show the *current* state - CLOSED at rest, OPEN once
    // pressed - never the action a click would perform.
    const label=o.controllable?(o.state?'OPEN':'CLOSED'):(o.state?'ON':'OFF');
    const ctrl=(o.controllable&&manual)
      ? `<button class="${o.state?'open':'close'}" onclick="set('${o.name}',${o.state?0:1})">${label}</button>`
      : `<div class="val ${o.state?'open':'shut'}">${label}</div>`;
    // The automation-controlled valve also shows its duty cycle, converted to
    // a run length in minutes, with an emoji giving an at-a-glance sense of
    // how wet the current cycle is.
    let duty='';
    if(s.automation&&o.name===s.automation.valve){
      const mins=s.automation.duty*s.automation.period_s/60;
      const minsText=(mins>0&&mins<1)?mins.toFixed(1):Math.round(mins);
      const emoji=s.automation.duty<=0?'🌵':s.automation.duty<=0.25?'💧':s.automation.duty<=0.5?'💦':s.automation.duty<=0.75?'🌧':'🌊';
      duty=`<div class="val duty">${minsText}m ${emoji}</div>`;
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
    const remain=Math.max(0,a.period_s-a.elapsed_s);
    document.getElementById('auto-info').innerHTML=
      `<div class="row"><div class="dot ${a.valve_on?'on':''}"></div><div class="name">Mist valve</div>`+
      `<div class="val">${a.valve_on?'MISTING':'idle'}</div></div>`+
      `<div class="row"><div class="name">Target / duty cycle</div>`+
      `<div class="val">${a.target_pct.toFixed(0)}% RH &nbsp; @ &nbsp; ${(a.duty*100).toFixed(0)}%</div></div>`+
      `<div class="row"><div class="name">Next cycle in</div><div class="val">${remain}s`+
      `${a.time_synced?'':' (clock not synced yet)'}</div></div>`;
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
            json += "\"controllable\":" + String(OUTPUTS[i].controllable ? "true" : "false") + "}";
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

        HumidityControlStatus automation = humidityControlStatus();
        json += "\"automation\":{";
        json += "\"valve\":\"" + String(HUMIDITY_VALVE_NAME) + "\",";
        json += "\"target_pct\":" + String(automation.targetPct, 1) + ",";
        json += "\"duty\":" + String(automation.dutyCycle, 3) + ",";
        json += "\"valve_on\":" + String(automation.valveOn ? "true" : "false") + ",";
        json += "\"period_s\":" + String(automation.periodS) + ",";
        json += "\"elapsed_s\":" + String(automation.elapsedS) + ",";
        json += "\"sensor_valid\":" + String(automation.sensorValid ? "true" : "false") + ",";
        json += "\"time_synced\":" + String(automation.timeSynced ? "true" : "false") + "}";

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
        // which would look like the command was lost.
        if (controlMode() != MODE_MANUAL)
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
