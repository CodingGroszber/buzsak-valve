#pragma once
#include <Arduino.h>

// ═════════════════════════════════════════════════════════════════════════════
//  VALVE CONTROL — the only place that switches relays
// ═════════════════════════════════════════════════════════════════════════════

// AUTOMATIC: the rungs in runAutomation() own the valves, and manual commands
//            are refused so the two cannot fight over a relay.
// MANUAL:    the valves are operated from the dashboard or the API, and the
//            automation is suspended.
enum ControlMode
{
    MODE_AUTOMATIC,
    MODE_MANUAL
};

ControlMode controlMode();
const char *controlModeName();

// Switches mode. Every handover closes all valves first, so neither side can
// inherit a valve the other left open.
void setControlMode(ControlMode mode);

// Configures the button and drives every relay to a known de-energised state.
// Call once from setup(), before the network comes up.
void valveControlSetup();

// Polls the button and runs the automation rungs. Call every loop() pass.
void valveControlLoop();

// Switches one controllable output. Every relay write in the firmware goes
// through here, so interlocks added inside it apply to the API, the button and
// the automation alike. Ignores non-controllable entries.
void valveSet(int outputIndex, bool on);

// Current state of an output, read back from the pin.
bool valveState(int outputIndex);

// Index of the output with this API name, or -1 if unknown.
int valveIndexByName(const char *name);
