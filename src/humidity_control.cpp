#include <Arduino.h>
#include <time.h>

#include "config.h"
#include "io_config.h"
#include "sensors.h"
#include "valve_control.h"
#include "humidity_control.h"

// ═════════════════════════════════════════════════════════════════════════════
//  HUMIDITY CONTROL
//
//  Time-proportioning control of the mist valve, the same idea as a slow PWM
//  heater controller: each HUMIDITY_PERIOD_S window, the valve runs for
//  duty*period starting at the period boundary, then stays shut for the rest.
//  duty 1.0 = runs the whole period, 0.5 = first half only, 0 = stays shut.
//
//  Periods are pinned to the wall clock rather than to millis() since boot, so
//  they fall on :00/:10/:20/:30/:40/:50 Hungarian time and survive a reset
//  without drifting. The CET/CEST offset is a whole number of hours, so
//  aligning on raw UTC epoch seconds already aligns the Budapest wall clock -
//  no timezone math needed here beyond having NTP configured (see
//  wifi_network.cpp).
//
//  The duty cycle itself is only ever recomputed once per period, from the
//  reading available at that instant:
//
//    * humidity >= target  -> duty drops to 0 immediately. The valve only
//      ever adds moisture, so once the target is reached the cheapest way to
//      save water is to stop right away rather than taper off.
//    * humidity <  target  -> duty increases by (deficit * gain), capped at
//      a max step per period. The cap matters because the probe sits in an
//      IP67 enclosure and lags the real air state by 1-5 minutes: a single
//      reading that is still catching up from the previous period must not
//      be allowed to slam the valve to full duty in one jump. Ramping
//      instead lets the (slow) feedback loop settle over a few periods.
// ═════════════════════════════════════════════════════════════════════════════

namespace
{
    float duty = 0.0f;
    long lastPeriodIndex = -1; // -1 = never computed, forces a recompute on boot

    // Real epoch seconds once NTP has answered, otherwise a millis()-based
    // stand-in so the PWM loop still runs (unaligned) before the clock syncs.
    uint32_t controlClockS(bool *synced)
    {
        time_t now = time(nullptr);
        if (now >= MIN_VALID_EPOCH)
        {
            *synced = true;
            return (uint32_t)now;
        }
        *synced = false;
        return millis() / 1000;
    }

    void recomputeDuty()
    {
        const SensorReading &reading = sensorReading(HUMIDITY_SENSOR_INDEX);

        if (!reading.valid)
        {
            // Can't verify humidity - fail safe rather than keep misting on
            // a stale reading, which would waste water at best.
            duty = 0.0f;
            Serial.println("[HUMID] sensor invalid - duty -> 0.00");
            return;
        }

        float error = HUMIDITY_TARGET_PCT - reading.humidityPct; // >0 = too dry
        if (error <= 0.0f)
        {
            duty = 0.0f;
        }
        else
        {
            float step = min(error * HUMIDITY_DUTY_GAIN, HUMIDITY_DUTY_MAX_STEP);
            duty = constrain(duty + step, 0.0f, 1.0f);
        }

        Serial.printf("[HUMID] %.1f%%RH (target %.0f%%) -> duty %.2f\n",
                      reading.humidityPct, HUMIDITY_TARGET_PCT, duty);
    }

    // Shared by humidityControlLoop() and humidityControlStatus() so both
    // agree on where the current period started, without either one owning
    // side effects on the other.
    void periodPhase(uint32_t clockS, long *periodIndex, uint32_t *elapsedS)
    {
        *periodIndex = (long)(clockS / HUMIDITY_PERIOD_S);
        *elapsedS = clockS - (uint32_t)(*periodIndex) * HUMIDITY_PERIOD_S;
    }
} // namespace

void humidityControlSetup()
{
    duty = 0.0f;
    lastPeriodIndex = -1;
}

void humidityControlLoop()
{
    bool synced;
    uint32_t clockS = controlClockS(&synced);

    long periodIndex;
    uint32_t elapsedS;
    periodPhase(clockS, &periodIndex, &elapsedS);

    // New period boundary crossed (or first pass ever): sample and adjust.
    if (periodIndex != lastPeriodIndex)
    {
        lastPeriodIndex = periodIndex;
        recomputeDuty();
    }

    uint32_t onDurationS = (uint32_t)(duty * HUMIDITY_PERIOD_S);
    bool shouldBeOn = elapsedS < onDurationS;

    int idx = valveIndexByName(HUMIDITY_VALVE_NAME);
    if (idx >= 0 && valveState(idx) != shouldBeOn)
        valveSet(idx, shouldBeOn);
}

HumidityControlStatus humidityControlStatus()
{
    bool synced;
    uint32_t clockS = controlClockS(&synced);

    long periodIndex;
    uint32_t elapsedS;
    periodPhase(clockS, &periodIndex, &elapsedS);

    const SensorReading &reading = sensorReading(HUMIDITY_SENSOR_INDEX);
    int idx = valveIndexByName(HUMIDITY_VALVE_NAME);

    HumidityControlStatus status{};
    status.timeSynced = synced;
    status.sensorValid = reading.valid;
    status.humidityPct = reading.humidityPct;
    status.targetPct = HUMIDITY_TARGET_PCT;
    status.dutyCycle = duty;
    status.valveOn = idx >= 0 && valveState(idx);
    status.periodS = HUMIDITY_PERIOD_S;
    status.elapsedS = elapsedS;
    return status;
}
