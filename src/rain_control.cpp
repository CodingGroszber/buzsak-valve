#include <Arduino.h>
#include <Preferences.h>
#include <time.h>

#include "config.h"
#include "io_config.h"
#include "valve_control.h"
#include "rain_control.h"

// ═════════════════════════════════════════════════════════════════════════════
//  RAIN CONTROL
//
//  Unlike the mist valve's duty-cycled control, the rain valve runs on a plain
//  daily schedule: open at the configured local time, shut the configured
//  duration later, no sensor feedback. Driven off localtime() rather
//  than raw epoch seconds (as humidity_control.cpp uses for its short, DST-
//  insensitive periods) because a once-a-day wall-clock time must track the
//  Europe/Budapest DST rule to stay at the same clock time year round.
// ═════════════════════════════════════════════════════════════════════════════

namespace
{
    bool valveOn = false;
    time_t runStartEpoch = 0; // epoch the current/last run began, 0 = never

    bool hasLastRun = false;
    uint32_t lastRunDurationS = 0;

    int startHour = RAIN_START_HOUR;
    int startMinute = RAIN_START_MINUTE;
    int durationMin = RAIN_DURATION_S / 60;

    constexpr const char *NVS_NAMESPACE = "rain";

    bool scheduleValid(int h, int m, int d)
    {
        return h >= 0 && h <= 23 && m >= 0 && m <= 59 && d >= 1 && d <= RAIN_DURATION_MAX_MIN;
    }

    // Sets the valve and updates book-keeping around the on/off edges, so the
    // loop and the status snapshot never disagree about what "last run" means.
    void setValve(bool on, time_t nowEpoch)
    {
        if (on == valveOn)
            return;

        int idx = valveIndexByName(RAIN_VALVE_NAME);
        if (idx >= 0)
            valveSet(idx, on);

        if (on)
        {
            runStartEpoch = nowEpoch;
            Serial.printf("[RAIN] run started\n");
        }
        else
        {
            lastRunDurationS = (uint32_t)(nowEpoch - runStartEpoch);
            hasLastRun = true;
            Serial.printf("[RAIN] run finished, duration %us\n", lastRunDurationS);
        }

        valveOn = on;
    }
} // namespace

void rainControlSetup()
{
    valveOn = false;
    runStartEpoch = 0;
    hasLastRun = false;
    lastRunDurationS = 0;

    Preferences prefs;
    if (prefs.begin(NVS_NAMESPACE, true))
    {
        int h = prefs.getInt("hour", RAIN_START_HOUR);
        int m = prefs.getInt("minute", RAIN_START_MINUTE);
        int d = prefs.getInt("dur_min", RAIN_DURATION_S / 60);
        prefs.end();
        if (scheduleValid(h, m, d))
        {
            startHour = h;
            startMinute = m;
            durationMin = d;
        }
    }
    Serial.printf("[RAIN] schedule %02d:%02d for %dm\n", startHour, startMinute, durationMin);
}

bool rainControlSetSchedule(int h, int m, int d)
{
    if (!scheduleValid(h, m, d))
        return false;

    startHour = h;
    startMinute = m;
    durationMin = d;

    Preferences prefs;
    if (prefs.begin(NVS_NAMESPACE, false))
    {
        prefs.putInt("hour", h);
        prefs.putInt("minute", m);
        prefs.putInt("dur_min", d);
        prefs.end();
    }
    Serial.printf("[RAIN] schedule set %02d:%02d for %dm\n", h, m, d);
    return true;
}

void rainControlLoop()
{
    time_t nowEpoch = time(nullptr);
    bool synced = nowEpoch >= MIN_VALID_EPOCH;

    // Without a synced clock, local hour/minute is meaningless - fail safe by
    // keeping the valve shut rather than guessing at the schedule.
    if (!synced)
    {
        setValve(false, nowEpoch);
        return;
    }

    struct tm lt;
    localtime_r(&nowEpoch, &lt);

    int minutesOfDay = lt.tm_hour * 60 + lt.tm_min;
    int startMinutes = startHour * 60 + startMinute;

    // Modular distance since the start so a run may cross midnight (e.g. 23:50 for 30m).
    bool shouldBeOn = (minutesOfDay - startMinutes + 1440) % 1440 < durationMin;
    setValve(shouldBeOn, nowEpoch);
}

RainControlStatus rainControlStatus()
{
    time_t nowEpoch = time(nullptr);

    RainControlStatus status{};
    status.timeSynced = nowEpoch >= MIN_VALID_EPOCH;
    status.startHour = startHour;
    status.startMinute = startMinute;
    status.durationS = (uint32_t)durationMin * 60;
    status.hasLastRun = hasLastRun;
    status.lastRunDurationS = lastRunDurationS;

    int idx = valveIndexByName(RAIN_VALVE_NAME);
    status.valveOn = idx >= 0 && valveState(idx);
    return status;
}
