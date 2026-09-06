#include <Arduino.h>
#include <ArduinoOTA.h>
#include <WiFi.h>

#include "config.h"
#include "io_config.h"
#include "secrets.h"
#include "valve_control.h"
#include "wifi_network.h"

// ═════════════════════════════════════════════════════════════════════════════
//  WIFI AND ARDUINO OTA
//
//  Responsibilities are split by transport: this module owns the WiFi
//  association and the UDP-based ArduinoOTA updater, while web.cpp owns
//  everything served over HTTP (dashboard, JSON API, ElegantOTA).
//
//  Credentials come from include/secrets.h, which is gitignored. Copy
//  secrets.example.h and fill it in before the first build.
// ═════════════════════════════════════════════════════════════════════════════

namespace
{
    // Blocks until associated, blinking the status LED at 2 Hz. Blocking here is
    // deliberate: no valve should be commanded before the controller is
    // reachable, and setup() has nothing else to do meanwhile.
    void connectWiFi()
    {
        Serial.print("[WIFI] connecting");
        WiFi.mode(WIFI_STA);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

        while (WiFi.status() != WL_CONNECTED)
        {
            digitalWrite(STATUS_LED_PIN, HIGH);
            delay(WIFI_BLINK_HALF_PERIOD_MS);
            digitalWrite(STATUS_LED_PIN, LOW);
            delay(WIFI_BLINK_HALF_PERIOD_MS);
            Serial.print(".");
        }

        digitalWrite(STATUS_LED_PIN, HIGH); // solid = associated
        Serial.println("\n[WIFI] connected, IP " + WiFi.localIP().toString());
    }

    void setupArduinoOta()
    {
        ArduinoOTA.setHostname(OTA_HOSTNAME);
        ArduinoOTA.setPassword(OTA_PASSWORD);

        // Safety: never leave a valve energised through a firmware swap. An
        // interrupted update would otherwise strand it open until someone
        // noticed the flooding.
        ArduinoOTA.onStart([]()
                           {
            for (int i = 0; i < NUM_OUTPUTS; i++)
                valveSet(i, false);
            Serial.println("[OTA] starting - all valves forced OFF"); });

        ArduinoOTA.onEnd([]()
                         { Serial.println("[OTA] complete, rebooting"); });

        ArduinoOTA.onProgress([](unsigned int done, unsigned int total)
                              { Serial.printf("[OTA] %u%%\n", done / (total / 100)); });

        ArduinoOTA.onError([](ota_error_t error)
                           { Serial.printf("[OTA] error %u\n", error); });

        ArduinoOTA.begin();
    }
} // namespace

void wifiSetup()
{
    pinMode(STATUS_LED_PIN, OUTPUT);
    connectWiFi();
    setupArduinoOta();
}

void wifiLoop()
{
    ArduinoOTA.handle();

    // Watchdog: the ESP32 does not always recover an association on its own,
    // for instance after the access point restarts.
    if (WiFi.status() != WL_CONNECTED)
    {
        digitalWrite(STATUS_LED_PIN, LOW);
        Serial.println("[WIFI] lost, reconnecting");
        connectWiFi();
    }
}
