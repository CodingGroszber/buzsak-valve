#pragma once

// Initialise the programmable button and leave all relays de-energised.
// Call once from setup().
void logicSetup();

// Poll the button (toggles the first relay) and, when RELAY_TEST_ENABLED=1,
// run the random one-relay-per-second click-test.
void logicLoop();
