#pragma once
#include <Arduino.h>
#include "config.h"

// ═════════════════════════════════════════════════════════════════════════════
//  IO INVENTORY — shared types and extern declarations
//
//  The OUTPUTS[] and SENSORS[] tables are defined in main.cpp. Editing those
//  two arrays is the only thing needed to add or remove a relay or a probe:
//  the API, the dashboard, the button and the polling loop all iterate them.
// ═════════════════════════════════════════════════════════════════════════════

// A switched output — a valve relay, or an indicator driven internally.
struct IOOutput
{
    const char *name;  // stable API key, e.g. "relay1"
    const char *label; // human-readable name shown on the dashboard
    int pin;           // GPIO number
    bool controllable; // true = switchable via the API, the button and the test
};

// An RS485 temperature/humidity probe.
struct IOSensor
{
    const char *name;  // stable API key, e.g. "sensor_a"
    const char *label; // human-readable name shown on the dashboard
    uint8_t address;   // Modbus slave address; probes ship as 1 and must differ
};

extern IOOutput OUTPUTS[];
extern const int NUM_OUTPUTS;

extern IOSensor SENSORS[];
extern const int NUM_SENSORS;
