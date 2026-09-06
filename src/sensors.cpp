#include <Arduino.h>
#include <driver/uart.h>

#include "config.h"
#include "io_config.h"
#include "sensors.h"

// ═════════════════════════════════════════════════════════════════════════════
//  RS485 / MODBUS RTU MASTER
//
//  Hardware
//  --------
//  A MAX3485 transceiver sits on UART2. Three signals matter:
//
//      DI  <- ESP32 TX (GPIO17)   data we transmit onto the bus
//      RO  -> ESP32 RX (GPIO16)   data the bus sends back
//      DE  <- ESP32    (GPIO4)    HIGH enables the driver (transmit)
//      RE                          tied to GND: receiver ALWAYS enabled
//
//  Because RE is grounded rather than switched together with DE, the receiver
//  stays on while we transmit and we therefore read back our own bytes. That is
//  intentional. The echo costs nothing (findFrame() skips it) and it doubles as
//  a permanent self-test: if the echo ever stops appearing, the fault is the
//  transceiver or its wiring, not the sensor. Diagnosing a silent bus without
//  that signal is considerably harder.
//
//  Protocol
//  --------
//  Modbus RTU at 9600 8N1. A master request is always 8 bytes and a two
//  register reply is 9 bytes:
//
//      request   01 03 00 00 00 02 C4 0B
//                ^^ ^^ ^^^^^ ^^^^^ ^^^^^
//                |  |  |     |     CRC16, low byte first
//                |  |  |     register count = 2
//                |  |  start register = 0x0000 (humidity)
//                |  function 0x03, read holding registers
//                slave address
//
//      reply     01 03 04 02 30 01 0C FA 11
//                         ^^ ^^^^^ ^^^^^ ^^^^^
//                         |  |     |     CRC16
//                         |  |     temperature 0x010C = 268 -> 26.8 C
//                         |  humidity 0x0230 = 560 -> 56.0 %RH
//                         byte count
//
//  Both values are scaled by ten. Humidity is unsigned, but TEMPERATURE IS
//  SIGNED: sub-zero readings arrive as two's complement, so -9.7 C is sent as
//  0xFF9F. Parsing it unsigned would report +6543.9 C.
//
//  Timing
//  ------
//  Modbus frames the message with >=3.5 character times of silence, roughly
//  4 ms at 9600 baud. Two consequences:
//
//    * DE must not drop until the final stop bit has physically left the UART,
//      otherwise the tail of our request is truncated. Serial2.flush() blocks
//      until the shift register is empty, so it is mandatory between write()
//      and lowering DE.
//    * The reply arrives a few milliseconds later. Rather than blocking, the
//      poller checks for it on subsequent loop() passes.
// ═════════════════════════════════════════════════════════════════════════════

// Indexed in parallel with SENSORS[].
static SensorReading readings[MAX_SENSORS];

namespace
{
    // The poller alternates between waiting for the next slot and waiting for
    // a reply, so loop() never blocks on the bus.
    enum PollState
    {
        POLL_IDLE,   // counting down to the next request
        POLL_WAITING // request sent, collecting the response
    };

    PollState state = POLL_IDLE;
    int current = 0;            // index into SENSORS[], round-robin
    uint32_t lastPollMs = 0;    // when the last request went out
    uint32_t requestSentMs = 0; // start of the response timeout window

    uint8_t rx[SENSOR_RX_BUFFER];
    size_t rxLen = 0;

    // ── CRC16/MODBUS ─────────────────────────────────────────────────────────
    // Reflected algorithm: shift right and XOR the reflected polynomial when a
    // one falls out. Seeded with 0xFFFF and appended low byte first.
    uint16_t crc16(const uint8_t *buf, size_t len)
    {
        uint16_t crc = MODBUS_CRC_INIT;
        for (size_t i = 0; i < len; i++)
        {
            crc ^= buf[i];
            for (int bit = 0; bit < 8; bit++)
                crc = (crc & 1) ? (crc >> 1) ^ MODBUS_CRC_POLY : (crc >> 1);
        }
        return crc;
    }

    // Formats the RX buffer as space-separated hex for the diagnostic fields.
    String hexDump()
    {
        String hex;
        for (size_t i = 0; i < rxLen; i++)
        {
            char byteText[4];
            sprintf(byteText, i ? " %02X" : "%02X", rx[i]);
            hex += byteText;
        }
        return hex;
    }

    // Copies the hex dump into the current sensor's reading record.
    void recordRaw()
    {
        char *out = readings[current].lastRaw;
        char *end = out + sizeof(readings[current].lastRaw);
        *out = '\0';
        for (size_t i = 0; i < rxLen && out + 4 < end; i++)
            out += sprintf(out, i ? " %02X" : "%02X", rx[i]);
    }

    // Appends the CRC and puts the frame on the wire.
    // Caller supplies the first six bytes; the last two are filled in here.
    void sendFrame(uint8_t *frame)
    {
        uint16_t crc = crc16(frame, MODBUS_REQUEST_LEN - 2);
        frame[6] = crc & 0xFF; // low byte first, per Modbus
        frame[7] = crc >> 8;

        // Drop anything left over from a previous transaction so the buffer
        // starts at a known point.
        while (Serial2.available())
            Serial2.read();

        digitalWrite(RS485_DE_PIN, HIGH);
        Serial2.write(frame, MODBUS_REQUEST_LEN);
        Serial2.flush(); // must complete before DE falls, or the frame is cut
        digitalWrite(RS485_DE_PIN, LOW);

        rxLen = 0;
        requestSentMs = millis();
    }

    // Requests humidity + temperature in a single transaction.
    void sendReadRequest(uint8_t address)
    {
        uint8_t frame[MODBUS_REQUEST_LEN] = {
            address,
            MODBUS_FC_READ_HOLDING,
            REG_HUMIDITY >> 8, REG_HUMIDITY & 0xFF,
            SENSOR_READ_REG_COUNT >> 8, SENSOR_READ_REG_COUNT & 0xFF,
            0, 0};
        sendFrame(frame);
    }

    // Blocking drain of the UART, used by the commissioning helpers.
    void collectResponse(uint32_t windowMs)
    {
        uint32_t deadline = millis() + windowMs;
        while (millis() < deadline && rxLen < SENSOR_RX_BUFFER)
        {
            while (Serial2.available() && rxLen < SENSOR_RX_BUFFER)
                rx[rxLen++] = Serial2.read();
            delay(2);
        }
    }

    void failCurrent(const char *reason)
    {
        readings[current].valid = false;
        readings[current].errors++;
        readings[current].lastError = reason;
        Serial.printf("[485] %s: %s [%s]\n",
                      SENSORS[current].name, reason, readings[current].lastRaw);
    }

    // Searches the buffer for a well-formed reply from the expected slave.
    //
    // Scanning rather than assuming a fixed offset is what makes the always-on
    // receiver workable: the buffer normally begins with our own 8-byte echo,
    // and on a noisy bus it may also contain a stray turnaround byte. Requiring
    // a matching address, function code, byte count AND CRC makes a false
    // positive effectively impossible.
    //
    // Returns the offset of the reply, or -1 if the buffer holds no valid frame.
    int findFrame()
    {
        for (size_t i = 0; i + MODBUS_READ_REPLY_LEN <= rxLen; i++)
        {
            if (rx[i] != SENSORS[current].address ||
                rx[i + 1] != MODBUS_FC_READ_HOLDING ||
                rx[i + 2] != SENSOR_READ_BYTE_COUNT)
                continue;

            uint16_t crc = crc16(&rx[i], MODBUS_READ_REPLY_LEN - 2);
            if ((crc & 0xFF) == rx[i + 7] && (crc >> 8) == rx[i + 8])
                return (int)i;
        }
        return -1;
    }

    // Converts a validated frame into engineering units.
    void parseFrame(size_t offset)
    {
        // Humidity is unsigned; temperature must go through int16_t so that
        // two's complement values below freezing decode correctly.
        uint16_t rawHumidity = (rx[offset + 3] << 8) | rx[offset + 4];
        int16_t rawTemperature = (int16_t)((rx[offset + 5] << 8) | rx[offset + 6]);

        readings[current].humidityPct = rawHumidity / SENSOR_VALUE_SCALE;
        readings[current].temperatureC = rawTemperature / SENSOR_VALUE_SCALE;
        readings[current].valid = true;
        readings[current].lastOkMs = millis();
        readings[current].lastError = "";

        Serial.printf("[485] %s: %.1f C  %.1f %%RH\n",
                      SENSORS[current].name,
                      readings[current].temperatureC,
                      readings[current].humidityPct);
    }
} // namespace

void sensorsSetup()
{
    for (int i = 0; i < NUM_SENSORS; i++)
        readings[i].lastError = "";

    pinMode(RS485_DE_PIN, OUTPUT);
    digitalWrite(RS485_DE_PIN, LOW); // idle in receive
    Serial2.begin(RS485_BAUD, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);

    lastPollMs = millis();
}

void sensorsLoop()
{
    if (NUM_SENSORS <= 0)
        return;

    uint32_t now = millis();

    // ── Idle: wait out the interval, then transmit the next request ──────────
    if (state == POLL_IDLE)
    {
        if (now - lastPollMs < SENSOR_POLL_INTERVAL_MS)
            return;

        lastPollMs = now;
        sendReadRequest(SENSORS[current].address);
        state = POLL_WAITING;
        return; // the reply cannot have arrived yet
    }

    // ── Waiting: accumulate bytes across successive loop() passes ────────────
    while (Serial2.available() && rxLen < SENSOR_RX_BUFFER)
        rx[rxLen++] = Serial2.read();

    int offset = findFrame();
    if (offset >= 0)
    {
        recordRaw();
        parseFrame((size_t)offset);
    }
    else if (now - requestSentMs < SENSOR_RESPONSE_TIMEOUT_MS)
    {
        return; // still inside the response window, keep collecting
    }
    else
    {
        // "timeout" means not a single byte arrived, which points at the
        // transceiver. "noframe" means bytes arrived but none formed a valid
        // reply — typically our echo alone, or two probes sharing an address
        // and corrupting each other.
        recordRaw();
        failCurrent(rxLen == 0 ? "timeout" : "noframe");
    }

    current = (current + 1) % NUM_SENSORS;
    state = POLL_IDLE;
}

const SensorReading &sensorReading(int index)
{
    return readings[index];
}

// ═════════════════════════════════════════════════════════════════════════════
//  COMMISSIONING HELPERS
//
//  These block for up to a few seconds and are meant for manual use through the
//  HTTP API while wiring up a bus, not for the normal polling path.
// ═════════════════════════════════════════════════════════════════════════════

String rs485Probe(uint8_t address, bool loopback)
{
    // Internal loopback ties the UART's TX to its own RX inside the ESP32,
    // bypassing the pins entirely. A correct echo therefore proves the UART
    // configuration, the frame layout and the CRC, independently of any wiring.
    if (loopback)
        uart_set_loop_back(UART_NUM_2, true);

    sendReadRequest(address);
    collectResponse(SENSOR_RESPONSE_TIMEOUT_MS);

    if (loopback)
        uart_set_loop_back(UART_NUM_2, false);

    String hex = hexDump();

    state = POLL_IDLE;
    lastPollMs = millis();
    return hex;
}

String rs485Scan(uint8_t maxAddr)
{
    String out = "[";
    bool first = true;

    for (size_t b = 0; b < RS485_SCAN_BAUD_COUNT; b++)
    {
        Serial2.updateBaudRate(RS485_SCAN_BAUDS[b]);
        delay(5); // let the divider settle before transmitting

        for (uint8_t addr = MODBUS_ADDRESS_MIN; addr <= maxAddr; addr++)
        {
            sendReadRequest(addr);
            collectResponse(SENSOR_SCAN_WINDOW_MS);
            if (rxLen == 0)
                continue;

            if (!first)
                out += ",";
            first = false;
            out += "{\"baud\":" + String(RS485_SCAN_BAUDS[b]) +
                   ",\"addr\":" + String(addr) +
                   ",\"raw\":\"" + hexDump() + "\"}";
        }
    }

    Serial2.updateBaudRate(RS485_BAUD);
    out += "]";

    state = POLL_IDLE;
    lastPollMs = millis();
    return out;
}

String rs485SetAddress(uint8_t from, uint8_t to)
{
    // Function 0x06 writes a single register. A successful write is confirmed
    // by the slave echoing the request back verbatim.
    uint8_t frame[MODBUS_REQUEST_LEN] = {
        from,
        MODBUS_FC_WRITE_SINGLE,
        REG_DEVICE_ADDRESS >> 8, REG_DEVICE_ADDRESS & 0xFF,
        0x00, to,
        0, 0};

    sendFrame(frame);
    collectResponse(SENSOR_RESPONSE_TIMEOUT_MS);

    String hex = hexDump();

    state = POLL_IDLE;
    lastPollMs = millis();
    return hex;
}
