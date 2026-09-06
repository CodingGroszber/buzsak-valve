#pragma once
#include <Arduino.h>

// ── Firmware version ─────────────────────────────────────────
#define FIRMWARE_VERSION "v0.7"

// ═════════════════════════════════════════════════════════════
//  IO CONFIGURATION — shared types and extern declarations
//
//  Board: ESP32-ACDC-RELAY4-M  (a.k.a. ESP32_Relay X4 AC/DC)
//         ESP32-WROOM-32E, 4 MB flash, 4x SPDT relay, 1x LED
//
//  Relay 1 .. 4 : GPIO 32, 33, 25, 26  (HIGH = coil energised)
//  Status LED   : GPIO 23
//  Prog header  : 5V / TX / RX / GND / GND / IO0
//
//  The OUTPUTS[] array is defined in main.cpp — edit it there.
//  Set controllable=true to allow remote control from the API.
// ═════════════════════════════════════════════════════════════

struct IOOutput
{
    const char *name;  // API key       e.g. "relay1"
    const char *label; // Human label   e.g. "Relay 1"
    int pin;           // Hardware pin  e.g. 32
    bool controllable; // true = settable via POST /api/control
};

extern IOOutput OUTPUTS[];
extern const int NUM_OUTPUTS;

// ── RS485 / Modbus RTU sensors ──────────────────────────────
// LY485 temperature/humidity probes on a MAX3485 transceiver (UART2).
// DE and RE are wired together and driven by the UART's RTS line.
constexpr int RS485_RX_PIN = 16; // <- module RO
constexpr int RS485_TX_PIN = 17; // -> module DI
constexpr int RS485_DE_PIN = 4;  // -> module DE + RE
constexpr uint32_t RS485_BAUD = 9600;

struct IOSensor
{
    const char *name;  // API key       e.g. "th1"
    const char *label; // Human label   e.g. "Temp/Humidity 1"
    uint8_t address;   // Modbus slave address (factory default 1)
};

extern IOSensor SENSORS[];
extern const int NUM_SENSORS;

// Returns the index of the "status_led" output, or -1 if not present
int wifiLedIndex();

// ── Relay drive levels ───────────────────────────────────────
// Coils are driven through a darlington array: HIGH energizes the coil.
constexpr uint8_t RELAY_ON = HIGH;
constexpr uint8_t RELAY_OFF = LOW;
// ── Programmable button ───────────────────────────────────
// Shares GPIO0 with the boot strapping pin: held LOW at reset = download mode.
constexpr uint8_t BUTTON_PIN = 0;
constexpr uint8_t BUTTON_PRESSED = LOW;