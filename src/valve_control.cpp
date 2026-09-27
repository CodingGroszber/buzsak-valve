#include <Arduino.h>
#include <esp_random.h>

#include "config.h"
#include "io_config.h"
#include "sensors.h"
#include "valve_control.h"

// ═════════════════════════════════════════════════════════════════════════════
//  VALVE CONTROL
//
//  Owns every relay write in the firmware. The HTTP API, the programmable
//  button and the automation rungs all funnel through valveSet(), which gives a
//  single place to add interlocks later — a "only one zone open at a time" rule
//  or a minimum gap between switching operations belong there, not scattered
//  across the callers.
//
//  Sensor-driven automation goes in runAutomation() at the bottom of this file.
// ═════════════════════════════════════════════════════════════════════════════

#ifndef RELAY_TEST_ENABLED
#define RELAY_TEST_ENABLED 0
#endif

namespace
{
    ControlMode mode = BOOT_IN_AUTOMATIC_MODE ? MODE_AUTOMATIC : MODE_MANUAL;

    uint32_t lastButtonMs = 0;
    bool buttonWasDown = false;

    void setAllRelays(uint8_t level)
    {
        for (int i = 0; i < NUM_OUTPUTS; i++)
            if (OUTPUTS[i].controllable)
                digitalWrite(OUTPUTS[i].pin, level);
    }

    int firstRelayIndex()
    {
        for (int i = 0; i < NUM_OUTPUTS; i++)
            if (OUTPUTS[i].controllable)
                return i;
        return -1;
    }

#if RELAY_TEST_ENABLED
    // ── Relay click-test ─────────────────────────────────────────────────────
    // A commissioning aid: exactly one switching action per second. Every relay
    // is switched ON in random order, then OFF in random order, then it repeats.
    // Useful for confirming wiring and hearing each contact, but it must be off
    // before real valves are attached — switching a 1" valve once per second
    // hammers the pipework.
    uint32_t lastStepMs = 0;
    bool switchingOn = true;

    // Picks a random controllable output not yet at `level`, or -1 when the
    // phase is complete. Two passes: count the candidates, then walk to the
    // n-th one. Avoids needing a scratch array sized at compile time.
    int pickRandomRelay(uint8_t level)
    {
        int count = 0;
        for (int i = 0; i < NUM_OUTPUTS; i++)
            if (OUTPUTS[i].controllable && digitalRead(OUTPUTS[i].pin) != level)
                count++;

        if (count == 0)
            return -1;

        int nth = esp_random() % count; // hardware RNG, no seeding required
        for (int i = 0; i < NUM_OUTPUTS; i++)
            if (OUTPUTS[i].controllable && digitalRead(OUTPUTS[i].pin) != level)
                if (nth-- == 0)
                    return i;

        return -1;
    }

    void restartCycle()
    {
        setAllRelays(RELAY_OFF);
        switchingOn = true;
        lastStepMs = millis();
    }

    void relayCycleLoop(uint32_t now)
    {
        if (now - lastStepMs < RELAY_TEST_STEP_MS)
            return;
        lastStepMs = now;

        uint8_t target = switchingOn ? RELAY_ON : RELAY_OFF;
        int idx = pickRandomRelay(target);

        if (idx < 0)
        {
            // Phase finished: flip direction and act immediately, so the
            // turnaround does not cost a beat of the one-per-second rhythm.
            switchingOn = !switchingOn;
            target = switchingOn ? RELAY_ON : RELAY_OFF;
            idx = pickRandomRelay(target);
            if (idx < 0)
                return; // no controllable outputs at all
        }

        valveSet(idx, target == RELAY_ON);
    }
#endif

    // Button action. Debounced falling edge, dispatched from valveControlLoop().
    void onButtonPressed()
    {
#if RELAY_TEST_ENABLED
        restartCycle();
        Serial.println("[BTN] all relays OFF, click-test restarted");
#else
        // Ignored in automatic mode: a press must not fight the automation.
        if (mode != MODE_MANUAL)
        {
            Serial.println("[BTN] ignored - controller is in automatic mode");
            return;
        }

        int idx = firstRelayIndex();
        if (idx < 0)
            return;
        valveSet(idx, !valveState(idx));
#endif
    }

    // ── Automation rungs ─────────────────────────────────────────────────────
    // Sensor-driven valve logic belongs here. Readings come from
    // sensorReading(i); always check `.valid` first, because a probe that has
    // dropped off the bus keeps its last values and acting on stale humidity
    // would leave a valve open. Nothing is implemented yet — the irrigation
    // rules are still to be defined.
    void runAutomation()
    {
    }
} // namespace

void valveControlSetup()
{
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    for (int i = 0; i < NUM_OUTPUTS; i++)
    {
        pinMode(OUTPUTS[i].pin, OUTPUT);
        digitalWrite(OUTPUTS[i].pin, LOW);
    }
}

void valveControlLoop()
{
    uint32_t now = millis();

    // ── Button: act on the falling edge, once the contact has settled ────────
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

    // Automation only owns the valves in automatic mode; in manual the operator
    // does, and the rungs stay out of the way entirely.
    if (mode == MODE_AUTOMATIC)
        runAutomation();
}

ControlMode controlMode()
{
    return mode;
}

const char *controlModeName()
{
    return mode == MODE_MANUAL ? "manual" : "automatic";
}

void setControlMode(ControlMode next)
{
    if (next == mode)
        return;

    mode = next;

    // Closing on every handover keeps the two owners from inheriting each
    // other's state: manual must not start with whatever the rungs left open,
    // and the automation must not silently adopt a manual override.
    setAllRelays(RELAY_OFF);
    Serial.printf("[MODE] %s - all valves closed\n", controlModeName());
}

void valveSet(int outputIndex, bool on)
{
    if (outputIndex < 0 || outputIndex >= NUM_OUTPUTS)
        return;
    if (!OUTPUTS[outputIndex].controllable)
        return;

    digitalWrite(OUTPUTS[outputIndex].pin, on ? RELAY_ON : RELAY_OFF);
    Serial.printf("[VALVE] %s -> %s\n", OUTPUTS[outputIndex].name, on ? "ON" : "OFF");
}

bool valveState(int outputIndex)
{
    if (outputIndex < 0 || outputIndex >= NUM_OUTPUTS)
        return false;
    return digitalRead(OUTPUTS[outputIndex].pin) == RELAY_ON;
}

int valveIndexByName(const char *name)
{
    for (int i = 0; i < NUM_OUTPUTS; i++)
        if (strcmp(OUTPUTS[i].name, name) == 0)
            return i;
    return -1;
}
