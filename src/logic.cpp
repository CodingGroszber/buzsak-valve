#include <Arduino.h>
#include <esp_random.h>
#include "io_config.h"
#include "logic.h"

#ifndef RELAY_TEST_ENABLED
#define RELAY_TEST_ENABLED 0
#endif

// ── Programmable button ──────────────────────────────────────
static const uint32_t BUTTON_DEBOUNCE_MS = 50;

static uint32_t lastButtonMs = 0;
static bool buttonWasDown = false;

static void setAllRelays(uint8_t level)
{
    for (int i = 0; i < NUM_OUTPUTS; i++)
        if (OUTPUTS[i].controllable)
            digitalWrite(OUTPUTS[i].pin, level);
}

static int firstRelayIndex()
{
    for (int i = 0; i < NUM_OUTPUTS; i++)
        if (OUTPUTS[i].controllable)
            return i;
    return -1;
}

#if RELAY_TEST_ENABLED

// ── Relay click-test ─────────────────────────────────────────
// One switching action per second: first every relay is turned ON in random
// order, then every relay is turned OFF in random order, then it repeats.
//
// Needs the board to be properly powered (230 V AC / 5 V DC terminal) —
// the USB-TTL adapter cannot supply the four relay coils.
static const uint32_t STEP_INTERVAL_MS = 1000;

static uint32_t lastStepMs = 0;
static bool switchingOn = true;

// Picks a random controllable output that is not yet at `level` (-1 if none left)
static int pickRandomRelay(uint8_t level)
{
    int count = 0;
    for (int i = 0; i < NUM_OUTPUTS; i++)
        if (OUTPUTS[i].controllable && digitalRead(OUTPUTS[i].pin) != level)
            count++;

    if (count == 0)
        return -1;

    int nth = esp_random() % count;
    for (int i = 0; i < NUM_OUTPUTS; i++)
        if (OUTPUTS[i].controllable && digitalRead(OUTPUTS[i].pin) != level)
            if (nth-- == 0)
                return i;

    return -1;
}

static void restartCycle()
{
    setAllRelays(RELAY_OFF);
    switchingOn = true;
    lastStepMs = millis();
}

static void relayCycleLoop(uint32_t now)
{
    if (now - lastStepMs < STEP_INTERVAL_MS)
        return;
    lastStepMs = now;

    uint8_t target = switchingOn ? RELAY_ON : RELAY_OFF;
    int idx = pickRandomRelay(target);
    if (idx < 0)
    {
        switchingOn = !switchingOn; // phase done — turn around without losing a beat
        target = switchingOn ? RELAY_ON : RELAY_OFF;
        idx = pickRandomRelay(target);
        if (idx < 0)
            return;
    }

    digitalWrite(OUTPUTS[idx].pin, target);
    Serial.printf("[TEST] %s -> %s\n", OUTPUTS[idx].name, switchingOn ? "ON" : "OFF");
}

#endif

static void onButtonPressed()
{
#if RELAY_TEST_ENABLED
    restartCycle();
    Serial.println("[BTN] all relays OFF, cycle restarted");
#else
    int idx = firstRelayIndex();
    if (idx < 0)
        return;

    bool on = digitalRead(OUTPUTS[idx].pin);
    digitalWrite(OUTPUTS[idx].pin, on ? RELAY_OFF : RELAY_ON);
    Serial.printf("[BTN] %s -> %s\n", OUTPUTS[idx].name, on ? "OFF" : "ON");
#endif
}

void logicSetup()
{
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    setAllRelays(RELAY_OFF);
}

void logicLoop()
{
    uint32_t now = millis();

    bool down = (digitalRead(BUTTON_PIN) == BUTTON_PRESSED);
    if (down != buttonWasDown && now - lastButtonMs >= BUTTON_DEBOUNCE_MS)
    {
        lastButtonMs = now;
        buttonWasDown = down;
        if (down)
            onButtonPressed();
    }

#if RELAY_TEST_ENABLED
    relayCycleLoop(now);
#endif
}
