#include <Arduino.h>

#include "config.h"
#include "io_config.h"
#include "sensors.h"
#include "valve_control.h"
#include "web.h"
#include "wifi_network.h"

// ═════════════════════════════════════════════════════════════════════════════
//  GARDEN VALVE CONTROL — ESP32-ACDC-RELAY4-M
//
//  This file is the wiring diagram of the firmware: it declares what hardware
//  exists and the order the modules run in. Behaviour lives in the modules:
//
//      valve_control.cpp   relay switching, button, automation rungs
//      sensors.cpp         RS485 Modbus RTU master
//      wifi_network.cpp    WiFi association + ArduinoOTA
//      web.cpp             dashboard, JSON API, ElegantOTA
//
//  Constants belong in config.h. Adding hardware means editing the two tables
//  below and nothing else.
// ═════════════════════════════════════════════════════════════════════════════

// ── Outputs ──────────────────────────────────────────────────────────────────
// controllable=false keeps an entry visible on the dashboard while excluding it
// from the API, the button and the click-test — used for the status LED, which
// the WiFi module drives.
IOOutput OUTPUTS[] = {
    {"relay1", "Valve 1", RELAY_1_PIN, true},
    {"relay2", "Valve 2", RELAY_2_PIN, true},
    {"relay3", "Valve 3", RELAY_3_PIN, true},
    {"relay4", "Valve 4", RELAY_4_PIN, true},
    {"status_led", "Status LED", STATUS_LED_PIN, false},
};

const int NUM_OUTPUTS = sizeof(OUTPUTS) / sizeof(OUTPUTS[0]);

// ── RS485 probes ─────────────────────────────────────────────────────────────
// Addresses must be unique: the probes ship as address 1, and two answering at
// once corrupt each other's frames. See README section 2.7 for readdressing.
IOSensor SENSORS[] = {
    {"sensor_a", "Sensor-A", 1},
    {"sensor_b", "Sensor-B", 2},
};

const int NUM_SENSORS = sizeof(SENSORS) / sizeof(SENSORS[0]);

void setup()
{
    Serial.begin(115200);

    // Valves first: this drives every relay low, so a reset can never leave a
    // valve energised while the network is still coming up.
    valveControlSetup();

    wifiSetup(); // blocks until associated
    webSetup();
    sensorsSetup();
}

void loop()
{
    wifiLoop();         // ArduinoOTA + association watchdog
    webLoop();          // HTTP requests + ElegantOTA
    valveControlLoop(); // button + automation rungs
    sensorsLoop();      // RS485 polling state machine

    delay(SCAN_CYCLE_MS);
}
