#include <Arduino.h>

#include "io_config.h"
#include "wifi_network.h"
#include "logic.h"
#include "sensors.h"

// ═════════════════════════════════════════════════════════════
//  IO LAYOUT — Edit here to add/remove relays
// ═════════════════════════════════════════════════════════════

// ── OUTPUTS ───────────────────────────────────────────────────
IOOutput OUTPUTS[] = {
    {"relay1", "Relay 1", 32, true},
    {"relay2", "Relay 2", 33, true},
    {"relay3", "Relay 3", 25, true},
    {"relay4", "Relay 4", 26, true},
    {"status_led", "Status LED", 23, false},
};

const int NUM_OUTPUTS = sizeof(OUTPUTS) / sizeof(OUTPUTS[0]);

// ── RS485 SENSORS ───────────────────────────────────────
IOSensor SENSORS[] = {
    {"th1", "Temp/Humidity 1", 1},
    {"th2", "Temp/Humidity 2", 2},
};

const int NUM_SENSORS = sizeof(SENSORS) / sizeof(SENSORS[0]);

// ── WiFi LED helper ───────────────────────────────────────────
int wifiLedIndex()
{
    for (int i = 0; i < NUM_OUTPUTS; i++)
        if (String(OUTPUTS[i].name) == "status_led")
            return i;
    return -1;
}

void setup()
{
    Serial.begin(115200);

    // Initialise all outputs — relays start de-energised
    for (int i = 0; i < NUM_OUTPUTS; i++)
    {
        pinMode(OUTPUTS[i].pin, OUTPUT);
        digitalWrite(OUTPUTS[i].pin, LOW);
    }

    setupNetwork();
    logicSetup();
    sensorsSetup();
}

void loop()
{
    loopNetwork(); // OTA + HTTP + WiFi watchdog
    logicLoop();   // button + relay click-test (RELAY_TEST_ENABLED)
    sensorsLoop(); // RS485 Modbus polling
    delay(50);     // 50 ms scan cycle
}
