#pragma once
#include <Arduino.h>

struct SensorReading
{
    float temperatureC;
    float humidityPct;
    bool valid;            // last poll succeeded
    uint32_t lastOkMs;     // millis() of the last good frame, 0 = never
    uint16_t errors;       // cumulative failed polls
    const char *lastError; // "", "timeout", "noframe"
    char lastRaw[64];      // hex dump of the last RX buffer, for bring-up
};

// Initialise UART2 in RS485 half-duplex mode. Call once from setup().
void sensorsSetup();

// Drive the polling state machine. Call every iteration of loop().
void sensorsLoop();

const SensorReading &sensorReading(int index);

// Blocking single transaction used by GET /api/rs485 during bring-up.
// Returns a hex dump of whatever came back. With loopback=true the UART is
// looped internally, so a correct echo proves the TX path independent of wiring.
String rs485Probe(uint8_t address, bool loopback = false);

// Sweeps every supported baud rate against addresses 1..maxAddr and reports
// anything that answers. Blocking, takes several seconds. Returns a JSON array.
String rs485Scan(uint8_t maxAddr);

// Writes register 0x0100 to change a probe's Modbus address (function 0x06).
// Only one sensor may be on the bus, or they will all take the new address.
String rs485SetAddress(uint8_t from, uint8_t to);
