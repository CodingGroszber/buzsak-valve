#pragma once
#include <Arduino.h>

// ═════════════════════════════════════════════════════════════════════════════
//  VALVE CONTROL — the only place that switches relays
// ═════════════════════════════════════════════════════════════════════════════

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
