#pragma once
#include <Arduino.h>

// ═════════════════════════════════════════════════════════════════════════════
//  SENSORS — RS485 / Modbus RTU master for the LY485 humidity probes
// ═════════════════════════════════════════════════════════════════════════════

// Latest state of one probe. Diagnostic fields are deliberately kept alongside
// the values so a misbehaving bus can be investigated purely over HTTP.
struct SensorReading
{
    float temperatureC;
    float humidityPct;
    bool valid;            // the most recent poll produced a CRC-valid frame
    uint32_t lastOkMs;     // millis() of the last good frame, 0 = never
    uint16_t errors;       // cumulative failed polls since boot
    const char *lastError; // "", "timeout" or "noframe"
    char lastRaw[64];      // hex dump of the last RX buffer
};

// Configures UART2 and the DE pin. Call once from setup().
void sensorsSetup();

// Advances the non-blocking polling state machine. Call every loop() pass.
void sensorsLoop();

// Latest reading for SENSORS[index].
const SensorReading &sensorReading(int index);

// ── Commissioning helpers, exposed through the HTTP API ──────────────────────

// Performs one blocking read and returns a hex dump of everything received,
// echo included. With loopback=true the UART is looped internally, which proves
// the transmit path without involving the transceiver or the wiring.
String rs485Probe(uint8_t address, bool loopback = false);

// Sweeps every supported baud rate against addresses 1..maxAddr and reports
// whatever answers. Blocking, takes several seconds. Returns a JSON array.
String rs485Scan(uint8_t maxAddr);

// Changes a probe's Modbus address by writing REG_DEVICE_ADDRESS.
// Only one sensor may be on the bus, otherwise they all take the new address.
String rs485SetAddress(uint8_t from, uint8_t to);
