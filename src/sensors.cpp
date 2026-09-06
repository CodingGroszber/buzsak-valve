#include <Arduino.h>
#include <driver/uart.h>
#include "io_config.h"
#include "sensors.h"

// ── LY485 temperature/humidity probe — Modbus RTU ────────────
// Only function 0x03 is used. Registers 0x0000 = humidity x10 (unsigned),
// 0x0001 = temperature x10 (signed, two's complement).
static const uint8_t MODBUS_READ_HOLDING = 0x03;
static const uint8_t MODBUS_WRITE_SINGLE = 0x06;
static const uint16_t REG_HUMIDITY = 0x0000;
static const uint16_t REG_ADDRESS = 0x0100;
static const uint16_t REG_COUNT = 2;

static const uint32_t POLL_INTERVAL_MS = 5000;
static const uint32_t RESPONSE_TIMEOUT_MS = 300;
static const size_t FRAME_LEN = 9; // addr + fc + count + 4 data + 2 crc
static const size_t RX_BUF = 20;

static SensorReading readings[8];

namespace
{
    enum PollState
    {
        POLL_IDLE,
        POLL_WAITING
    };

    PollState state = POLL_IDLE;
    int current = 0;
    uint32_t lastPollMs = 0;
    uint32_t requestSentMs = 0;
    uint8_t rx[RX_BUF];
    size_t rxLen = 0;

    uint16_t crc16(const uint8_t *buf, size_t len)
    {
        uint16_t crc = 0xFFFF;
        for (size_t i = 0; i < len; i++)
        {
            crc ^= buf[i];
            for (int b = 0; b < 8; b++)
                crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : (crc >> 1);
        }
        return crc;
    }

    void recordRaw()
    {
        char *p = readings[current].lastRaw;
        char *end = p + sizeof(readings[current].lastRaw);
        *p = '\0';
        for (size_t i = 0; i < rxLen && p + 4 < end; i++)
            p += sprintf(p, i ? " %02X" : "%02X", rx[i]);
    }

    String hexDump()
    {
        String hex;
        for (size_t i = 0; i < rxLen; i++)
        {
            char b[4];
            sprintf(b, i ? " %02X" : "%02X", rx[i]);
            hex += b;
        }
        return hex;
    }

    void collectResponse(uint32_t windowMs)
    {
        uint32_t deadline = millis() + windowMs;
        while (millis() < deadline && rxLen < RX_BUF)
        {
            while (Serial2.available() && rxLen < RX_BUF)
                rx[rxLen++] = Serial2.read();
            delay(2);
        }
    }

    void sendFrame(uint8_t *frame)
    {
        uint16_t crc = crc16(frame, 6);
        frame[6] = crc & 0xFF;
        frame[7] = crc >> 8;

        while (Serial2.available())
            Serial2.read();

        digitalWrite(RS485_DE_PIN, HIGH);
        Serial2.write(frame, 8);
        Serial2.flush(); // must not release DE before the last bit is out
        digitalWrite(RS485_DE_PIN, LOW);

        rxLen = 0;
        requestSentMs = millis();
    }

    void sendReadRequest(uint8_t address)
    {
        uint8_t frame[8] = {address, MODBUS_READ_HOLDING,
                            REG_HUMIDITY >> 8, REG_HUMIDITY & 0xFF,
                            REG_COUNT >> 8, REG_COUNT & 0xFF, 0, 0};
        sendFrame(frame);
    }

    void failCurrent(const char *reason)
    {
        readings[current].valid = false;
        readings[current].errors++;
        readings[current].lastError = reason;
        Serial.printf("[485] %s: %s [%s]\n", SENSORS[current].name, reason, readings[current].lastRaw);
    }

    // Locates a valid response inside the RX buffer, skipping any TX echo.
    int findFrame()
    {
        for (size_t i = 0; i + FRAME_LEN <= rxLen; i++)
        {
            if (rx[i] != SENSORS[current].address || rx[i + 1] != MODBUS_READ_HOLDING || rx[i + 2] != 4)
                continue;
            uint16_t crc = crc16(&rx[i], 7);
            if ((crc & 0xFF) == rx[i + 7] && (crc >> 8) == rx[i + 8])
                return (int)i;
        }
        return -1;
    }

    void parseFrame(size_t off)
    {
        uint16_t humidity = (rx[off + 3] << 8) | rx[off + 4];
        int16_t temperature = (int16_t)((rx[off + 5] << 8) | rx[off + 6]);

        readings[current].humidityPct = humidity / 10.0f;
        readings[current].temperatureC = temperature / 10.0f;
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
    digitalWrite(RS485_DE_PIN, LOW); // receive by default
    Serial2.begin(RS485_BAUD, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);

    lastPollMs = millis();
}

void sensorsLoop()
{
    if (NUM_SENSORS <= 0)
        return;

    uint32_t now = millis();

    if (state == POLL_IDLE)
    {
        if (now - lastPollMs < POLL_INTERVAL_MS)
            return;
        lastPollMs = now;
        sendReadRequest(SENSORS[current].address);
        state = POLL_WAITING;
        return;
    }

    while (Serial2.available() && rxLen < RX_BUF)
        rx[rxLen++] = Serial2.read();

    int off = findFrame();
    if (off >= 0)
    {
        recordRaw();
        parseFrame((size_t)off);
    }
    else if (now - requestSentMs < RESPONSE_TIMEOUT_MS)
        return;
    else
    {
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

String rs485Probe(uint8_t address, bool loopback)
{
    if (loopback)
        uart_set_loop_back(UART_NUM_2, true);

    sendReadRequest(address);
    collectResponse(RESPONSE_TIMEOUT_MS);

    if (loopback)
        uart_set_loop_back(UART_NUM_2, false);

    String hex = hexDump();

    state = POLL_IDLE;
    lastPollMs = millis();
    return hex;
}

String rs485Scan(uint8_t maxAddr)
{
    static const uint32_t BAUDS[] = {9600, 4800, 19200, 2400, 14400, 1200};

    String out = "[";
    bool first = true;

    for (uint32_t baud : BAUDS)
    {
        Serial2.updateBaudRate(baud);
        delay(5);

        for (uint8_t addr = 1; addr <= maxAddr; addr++)
        {
            sendReadRequest(addr);
            collectResponse(200);
            if (rxLen == 0)
                continue;

            if (!first)
                out += ",";
            first = false;
            out += "{\"baud\":" + String(baud) +
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
    uint8_t frame[8] = {from, MODBUS_WRITE_SINGLE,
                        REG_ADDRESS >> 8, REG_ADDRESS & 0xFF,
                        0x00, to, 0, 0};
    sendFrame(frame);
    collectResponse(RESPONSE_TIMEOUT_MS);

    String hex = hexDump();

    state = POLL_IDLE;
    lastPollMs = millis();
    return hex;
}
