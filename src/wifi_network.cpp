#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoOTA.h>
#include <ElegantOTA.h>
#include "io_config.h"
#include "wifi_network.h"
#include "sensors.h"
#include "secrets.h"

// ── WiFi credentials ────────────────────────────────────
const char *ssid = WIFI_SSID;
const char *password = WIFI_PASSWORD;

// ── Web server on port 80 ────────────────────────────────────
WebServer server(80);

// ── Dashboard (kept small enough to live inline in Flash) ────
static const char INDEX_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Garden Valve Control</title>
<style>
body{font-family:system-ui,sans-serif;background:#111;color:#eee;margin:0;padding:1.5rem}
h1{font-size:1.2rem;margin:0 0 1rem}
.row{display:flex;align-items:center;gap:.75rem;padding:.6rem 0;border-bottom:1px solid #333}
.dot{width:.9rem;height:.9rem;border-radius:50%;background:#444}
.on{background:#3c3}
.name{flex:1}
button{background:#2a2a2a;color:#eee;border:1px solid #555;border-radius:.3rem;padding:.4rem .9rem;cursor:pointer}
button:hover{background:#3a3a3a}
footer{margin-top:1.5rem;font-size:.8rem;color:#888}
a{color:#6af}
</style></head><body>
<h1>Garden Valve Control</h1>
<div id="io">loading...</div>
<div id="sensors"></div>
<footer><span id="fw"></span> &middot; <a href="/update">OTA update</a></footer>
<script>
async function refresh(){
  const s=await (await fetch('/api/state')).json();
  document.getElementById('fw').textContent=s.firmware+' @ '+s.ip;
  document.getElementById('io').innerHTML=s.outputs.map(o=>
    `<div class="row"><div class="dot ${o.state?'on':''}"></div><div class="name">${o.label}</div>`+
    (o.controllable?`<button onclick="set('${o.name}',${o.state?0:1})">${o.state?'OFF':'ON'}</button>`:'')+
    `</div>`).join('');
  document.getElementById('sensors').innerHTML=(s.sensors||[]).map(x=>
    `<div class="row"><div class="dot ${x.ok?'on':''}"></div><div class="name">${x.label}</div>`+
    `<div>${x.ok?x.temperature_c+' &deg;C / '+x.humidity_pct+' %RH':(x.last_error||'--')}</div></div>`).join('');
}
async function set(name,state){
  await fetch('/api/control?name='+name+'&state='+state,{method:'POST'});
  refresh();
}
refresh();setInterval(refresh,2000);
</script></body></html>
)HTML";

// ── WiFi connect ─────────────────────────────────────────────
void connectWiFi()
{
    Serial.print("Connecting to WiFi");
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    int led = wifiLedIndex();
    while (WiFi.status() != WL_CONNECTED)
    {
        if (led >= 0)
        {
            digitalWrite(OUTPUTS[led].pin, HIGH);
            delay(250);
            digitalWrite(OUTPUTS[led].pin, LOW);
            delay(250);
        }
        else
            delay(500);
        Serial.print(".");
    }
    if (led >= 0)
        digitalWrite(OUTPUTS[led].pin, HIGH); // solid ON = connected
    Serial.println("\nWiFi connected! IP: " + WiFi.localIP().toString());
}

// ── GET / — serve the HTML dashboard from Flash ──────────────
static void handleRoot()
{
    server.send_P(200, "text/html", INDEX_HTML);
}

// ── GET /api/state ────────────────────────────────────────────
// Returns the full output state as JSON
static void handleApiState()
{
    String json = "{";
    json += "\"firmware\":\"" + String(FIRMWARE_VERSION) + "\",";
    json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";

    json += "\"outputs\":[";
    for (int i = 0; i < NUM_OUTPUTS; i++)
    {
        if (i)
            json += ",";
        bool s = digitalRead(OUTPUTS[i].pin);
        json += "{\"name\":\"" + String(OUTPUTS[i].name) + "\",";
        json += "\"label\":\"" + String(OUTPUTS[i].label) + "\",";
        json += "\"state\":" + String(s ? "true" : "false") + ",";
        json += "\"controllable\":" + String(OUTPUTS[i].controllable ? "true" : "false") + "}";
    }
    json += "],";

    json += "\"sensors\":[";
    for (int i = 0; i < NUM_SENSORS; i++)
    {
        if (i)
            json += ",";
        const SensorReading &r = sensorReading(i);
        json += "{\"name\":\"" + String(SENSORS[i].name) + "\",";
        json += "\"label\":\"" + String(SENSORS[i].label) + "\",";
        json += "\"address\":" + String(SENSORS[i].address) + ",";
        json += "\"ok\":" + String(r.valid ? "true" : "false") + ",";
        if (r.valid)
        {
            json += "\"temperature_c\":" + String(r.temperatureC, 1) + ",";
            json += "\"humidity_pct\":" + String(r.humidityPct, 1) + ",";
        }
        json += "\"errors\":" + String(r.errors) + ",";
        json += "\"last_error\":\"" + String(r.lastError) + "\",";
        json += "\"raw\":\"" + String(r.lastRaw) + "\",";
        json += "\"age_s\":" + String(r.lastOkMs ? (millis() - r.lastOkMs) / 1000 : 0) + "}";
    }
    json += "]";

    json += "}";
    server.send(200, "application/json", json);
}

// ── POST /api/control ─────────────────────────────────────────
// Query/form: name=<output_name>&state=<1|0>
// Only sets the output if controllable=true
static void handleApiControl()
{
    String name = server.arg("name");
    String stateStr = server.arg("state");

    if (name.isEmpty() || stateStr.isEmpty())
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Missing name or state\"}");
        return;
    }

    bool newState = (stateStr == "1" || stateStr == "true");

    for (int i = 0; i < NUM_OUTPUTS; i++)
    {
        if (name.equals(OUTPUTS[i].name))
        {
            if (!OUTPUTS[i].controllable)
            {
                server.send(403, "application/json", "{\"ok\":false,\"error\":\"Not controllable\"}");
                return;
            }
            digitalWrite(OUTPUTS[i].pin, newState ? RELAY_ON : RELAY_OFF);
            server.send(200, "application/json", "{\"ok\":true}");
            Serial.println("[CTRL] " + name + " -> " + String(newState ? "ON" : "OFF"));
            return;
        }
    }
    server.send(404, "application/json", "{\"ok\":false,\"error\":\"Output not found\"}");
}

// ── GET /api/rs485?addr=N ─ bring-up probe, returns the raw RX bytes ──
static void handleApiRs485()
{
    uint8_t addr = server.arg("addr").isEmpty() ? 1 : server.arg("addr").toInt();
    bool loopback = server.arg("loopback") == "1";
    String hex = rs485Probe(addr, loopback);
    server.send(200, "application/json",
                "{\"addr\":" + String(addr) + ",\"loopback\":" + String(loopback ? "true" : "false") +
                    ",\"raw\":\"" + hex + "\"}");
}

// ── GET /api/rs485/scan ─ sweep baud rates and addresses ──────────
static void handleApiRs485Scan()
{
    uint8_t maxAddr = server.arg("max").isEmpty() ? 8 : server.arg("max").toInt();
    server.send(200, "application/json", rs485Scan(maxAddr));
}

// ── Network setup (called once from main setup()) ─────────────
void setupNetwork()
{
    connectWiFi();

    // ArduinoOTA
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.onStart([]()
                       {
    // Safety: turn all controllable outputs OFF during flash
    for (int i = 0; i < NUM_OUTPUTS; i++)
      if (OUTPUTS[i].controllable) digitalWrite(OUTPUTS[i].pin, RELAY_OFF);
    Serial.println("OTA starting — relays forced OFF."); });
    ArduinoOTA.onEnd([]()
                     { Serial.println("OTA complete! Rebooting..."); });
    ArduinoOTA.onProgress([](unsigned int p, unsigned int t)
                          { Serial.printf("OTA: %u%%\n", p / (t / 100)); });
    ArduinoOTA.onError([](ota_error_t e)
                       { Serial.printf("OTA Error [%u]\n", e); });
    ArduinoOTA.begin();

    // HTTP routes
    server.on("/", HTTP_GET, handleRoot);
    server.on("/api/state", HTTP_GET, handleApiState);
    server.on("/api/control", HTTP_POST, handleApiControl);
    server.on("/api/rs485", HTTP_GET, handleApiRs485);
    server.on("/api/rs485/scan", HTTP_GET, handleApiRs485Scan);

    ElegantOTA.setAuth(OTA_HTTP_USER, OTA_HTTP_PASS);
    ElegantOTA.begin(&server);
    server.begin();

    Serial.println(">>> Firmware " + String(FIRMWARE_VERSION) + " — boot OK");
    Serial.println("  Status : http://" + WiFi.localIP().toString() + "/");
    Serial.println("  State  : http://" + WiFi.localIP().toString() + "/api/state");
    Serial.println("  Control: POST http://" + WiFi.localIP().toString() + "/api/control");
    Serial.println("  OTA    : http://" + WiFi.localIP().toString() + "/update");
}

// ── Network loop (called every iteration of main loop()) ──────
void loopNetwork()
{
    ArduinoOTA.handle();
    server.handleClient();
    ElegantOTA.loop();

    // WiFi watchdog
    if (WiFi.status() != WL_CONNECTED)
    {
        int led = wifiLedIndex();
        if (led >= 0)
            digitalWrite(OUTPUTS[led].pin, LOW);
        Serial.println("WiFi lost! Reconnecting...");
        connectWiFi();
    }
}
