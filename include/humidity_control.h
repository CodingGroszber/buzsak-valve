#pragma once
#include <Arduino.h>

// ═════════════════════════════════════════════════════════════════════════════
//  HUMIDITY CONTROL — automatic PWM-style duty cycling of the mist valve
//
//  Isolated from valve_control.cpp/.h, which only ever knows "the automation
//  rungs want this relay on/off". This module decides *what that state should
//  be*, from the RH sensor and the wall clock, and leaves relay ownership,
//  interlocks and the manual/automatic handover entirely to valve_control.
// ═════════════════════════════════════════════════════════════════════════════

// Snapshot for diagnostics and the dashboard. Recomputed on demand, not a
// live handle into the controller's internal state.
struct HumidityControlStatus
{
    bool timeSynced;   // false = periods are running on millis(), unaligned
    bool sensorValid;  // false = last reading was stale/absent
    float humidityPct; // last known reading, meaningless if !sensorValid
    float targetPct;   // HUMIDITY_TARGET_PCT
    float dutyCycle;   // 0..1, fraction of the period the valve runs
    bool valveOn;      // current commanded state of the mist valve
    uint32_t periodS;  // HUMIDITY_PERIOD_S
    uint32_t elapsedS; // seconds into the current period
};

// Resets the controller to idle (duty 0). Call once from setup().
void humidityControlSetup();

// Advances the duty-cycle controller and drives the mist valve. Call every
// pass of runAutomation(), i.e. only while the controller is in automatic
// mode - manual mode must not call this, valveControl owns the handover.
void humidityControlLoop();

// Read-only snapshot for the HTTP API / dashboard.
HumidityControlStatus humidityControlStatus();
