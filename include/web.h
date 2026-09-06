#pragma once
#include <Arduino.h>

// ═════════════════════════════════════════════════════════════════════════════
//  WEB — dashboard, JSON API and the ElegantOTA updater (HTTP, port 80)
// ═════════════════════════════════════════════════════════════════════════════

// Registers the routes, starts ElegantOTA and the HTTP server.
// Call once from setup(), after WiFi is up.
void webSetup();

// Services pending HTTP requests and the OTA upload. Call every loop() pass.
void webLoop();
