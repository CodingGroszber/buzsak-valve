#pragma once
#include <WebServer.h>

extern WebServer server;

// Connect to WiFi (blocks until connected, blinks status_led while waiting)
void connectWiFi();

// Initialise WiFi, ArduinoOTA, ElegantOTA, and the HTTP server.
// Call once from setup().
void setupNetwork();

// Drive OTA, HTTP, and the WiFi watchdog.
// Call every iteration of loop().
void loopNetwork();
