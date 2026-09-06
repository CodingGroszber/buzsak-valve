#include <Arduino.h>
#include <ElegantOTA.h>
#include <WebServer.h>
#include <WiFi.h>

#include "config.h"
#include "io_config.h"
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
h1{font-size:1.25rem;margin:0 0 1.25rem}
h2{font-size:.8rem;text-transform:uppercase;letter-spacing:.08em;color:#888;margin:1.5rem 0 .25rem}
.row{display:flex;align-items:center;gap:.75rem;padding:.6rem 0;border-bottom:1px solid #2a2a2a}
.dot{width:.9rem;height:.9rem;border-radius:50%;background:#444;flex:none}
.on{background:#3c3}
.bad{background:#c33}
.name{flex:1}
.val{font-variant-numeric:tabular-nums;color:#bbb}
button{background:#2a2a2a;color:#eee;border:1px solid #555;border-radius:.3rem;padding:.4rem .9rem;cursor:pointer}
button:hover{background:#3a3a3a}
footer{margin-top:1.75rem;font-size:.8rem;color:#888}
a{color:#6af}
</style></head><body>
<h1>Garden Valve Control</h1>
<h2>Valves</h2>
<div id="outputs">loading...</div>
<h2>Sensors</h2>
<div id="sensors"></div>
<footer><span id="fw"></span> &middot; <a href="/update">OTA update</a></footer>
<script>
async function refresh(){
  const s=await (await fetch('/api/state')).json();
  document.getElementById('fw').textContent=s.firmware+' @ '+s.ip;

  document.getElementById('outputs').innerHTML=s.outputs.map(o=>
    `<div class="row"><div class="dot ${o.state?'on':''}"></div><div class="name">${o.label}</div>`+
    (o.controllable?`<button onclick="set('${o.name}',${o.state?0:1})">${o.state?'OFF':'ON'}</button>`:'')+
    `</div>`).join('');

  document.getElementById('sensors').innerHTML=(s.sensors||[]).map(x=>
    `<div class="row"><div class="dot ${x.ok?'on':'bad'}"></div><div class="name">${x.label}</div>`+
    `<div class="val">${x.ok?x.temperature_c.toFixed(1)+' &deg;C &nbsp; '+x.humidity_pct.toFixed(1)+' %RH'
                        :(x.last_error||'no data')}</div></div>`).join('');
}
async function set(name,state){
  await fetch('/api/control?name='+name+'&state='+state,{method:'POST'});
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
        json += "]";

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

        valveSet(index, stateArg == "1" || stateArg == "true");
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
