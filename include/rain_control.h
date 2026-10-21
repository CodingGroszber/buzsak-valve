#pragma once
#include <Arduino.h>

// ═════════════════════════════════════════════════════════════════════════════
//  RAIN CONTROL — automatic once-a-day straight run of the rain valve
//
//  Isolated from valve_control.cpp/.h the same way humidity_control.cpp is:
//  this module only decides whether the rain valve should be open right now,
//  from the wall clock. Relay ownership, interlocks and the manual/automatic
//  handover stay entirely in valve_control.
// ═════════════════════════════════════════════════════════════════════════════

// Snapshot for diagnostics and the dashboard. Recomputed on demand, not a
// live handle into the controller's internal state.
struct RainControlStatus
{
    bool timeSynced; // false = wall clock not NTP-synced yet, valve held off
    bool valveOn;    // current commanded state of the rain valve
    int startHour;   // configured start, local time
    int startMinute;
    uint32_t durationS;        // configured run length
    bool hasLastRun;           // false = no run has completed since boot
    uint32_t lastRunDurationS; // actual duration of the most recently completed run
};

// Resets the controller to idle. Call once from setup().
void rainControlSetup();

// Advances the daily schedule and drives the rain valve. Call every pass of
// runAutomation(), i.e. only while the controller is in automatic mode.
void rainControlLoop();

// Sets and persists the daily schedule. Returns false (nothing changed) if the
// time is not a valid HH:MM or the duration is outside 1..RAIN_DURATION_MAX_MIN.
bool rainControlSetSchedule(int startHour, int startMinute, int durationMin);

// Read-only snapshot for the HTTP API / dashboard.
RainControlStatus rainControlStatus();
