#pragma once
#include <Arduino.h>

// ═════════════════════════════════════════════════════════════════════════════
//  CENTRAL CONFIGURATION
//
//  Every tunable constant in the firmware lives here: pin assignments, timings,
//  Modbus register numbers and web settings. Nothing else in the project should
//  contain a magic number.
//
//  Board: ESP32-ACDC-RELAY4-M (ESP32_Relay X4 AC/DC), ESP32-WROOM-32E, 4 MB.
// ═════════════════════════════════════════════════════════════════════════════

// ── Firmware identity ────────────────────────────────────────────────────────
#define FIRMWARE_VERSION "v0.8"

// ── Relay outputs ────────────────────────────────────────────────────────────
// The coils are driven through a darlington array, so a HIGH on the GPIO
// energises the coil and closes the COM->NO contact.
constexpr int RELAY_1_PIN = 32;
constexpr int RELAY_2_PIN = 33;
constexpr int RELAY_3_PIN = 25;
constexpr int RELAY_4_PIN = 26;

constexpr uint8_t RELAY_ON = HIGH;
constexpr uint8_t RELAY_OFF = LOW;

// ── Status LED ───────────────────────────────────────────────────────────────
// Blinks while associating with WiFi, solid once connected.
constexpr int STATUS_LED_PIN = 23;

// ── Programmable button ──────────────────────────────────────────────────────
// Shares GPIO0 with the boot strapping pin: if it is held LOW while the ESP32
// resets, the chip enters serial download mode instead of running the firmware.
constexpr int BUTTON_PIN = 0;
constexpr uint8_t BUTTON_PRESSED = LOW;

// ── RS485 transceiver (MAX3485 on UART2) ─────────────────────────────────────
// RE is hard-wired to GND on the module, so the receiver is permanently
// enabled and we always see our own transmission echoed back. See sensors.cpp.
constexpr int RS485_RX_PIN = 16;  // <- module RO
constexpr int RS485_TX_PIN = 17;  // -> module DI
constexpr int RS485_DE_PIN = 4;   // -> module DE (HIGH while transmitting)
constexpr uint32_t RS485_BAUD = 9600;

// ── Scheduling ───────────────────────────────────────────────────────────────
constexpr uint32_t SCAN_CYCLE_MS = 50;          // main loop() period
constexpr uint32_t BUTTON_DEBOUNCE_MS = 50;     // contact settle time
constexpr uint32_t RELAY_TEST_STEP_MS = 1000;   // click-test switching interval
constexpr uint32_t WIFI_BLINK_HALF_PERIOD_MS = 250;

// ── Sensor polling ───────────────────────────────────────────────────────────
constexpr uint32_t SENSOR_POLL_INTERVAL_MS = 5000;    // per sensor, round-robin
constexpr uint32_t SENSOR_RESPONSE_TIMEOUT_MS = 300;  // give up on a reply
constexpr uint32_t SENSOR_SCAN_WINDOW_MS = 200;       // shorter window when sweeping
constexpr size_t SENSOR_RX_BUFFER = 20;               // echo + reply + slack
constexpr int MAX_SENSORS = 8;                        // sizes the readings table

// ── Modbus RTU ───────────────────────────────────────────────────────────────
// The LY485 probes implement only these two function codes.
constexpr uint8_t MODBUS_FC_READ_HOLDING = 0x03;
constexpr uint8_t MODBUS_FC_WRITE_SINGLE = 0x06;

// A request is always 8 bytes: addr, fc, reg hi/lo, value hi/lo, crc lo/hi.
constexpr size_t MODBUS_REQUEST_LEN = 8;
// A 2-register read replies with 9: addr, fc, bytecount, 4 data, crc lo/hi.
constexpr size_t MODBUS_READ_REPLY_LEN = 9;

// CRC16/MODBUS: reflected polynomial, all-ones seed, transmitted low byte first.
constexpr uint16_t MODBUS_CRC_POLY = 0xA001;
constexpr uint16_t MODBUS_CRC_INIT = 0xFFFF;

constexpr uint8_t MODBUS_ADDRESS_MIN = 1;
constexpr uint8_t MODBUS_ADDRESS_MAX = 247;

// ── LY485 register map (see docs/manual.md) ──────────────────────────────────
constexpr uint16_t REG_HUMIDITY = 0x0000;        // x10, unsigned
constexpr uint16_t REG_TEMPERATURE = 0x0001;     // x10, signed two's complement
constexpr uint16_t REG_DEVICE_ADDRESS = 0x0100;  // read/write
constexpr uint16_t REG_BAUD_RATE = 0x0101;       // read/write

// Humidity and temperature are adjacent, so one transaction fetches both.
constexpr uint16_t SENSOR_READ_REG_COUNT = 2;
constexpr uint8_t SENSOR_READ_BYTE_COUNT = 4;

// Both registers carry the value multiplied by ten.
constexpr float SENSOR_VALUE_SCALE = 10.0f;

// Baud rates tried by the commissioning sweep, most likely first.
constexpr uint32_t RS485_SCAN_BAUDS[] = {9600, 4800, 19200, 2400, 14400, 1200};
constexpr size_t RS485_SCAN_BAUD_COUNT = sizeof(RS485_SCAN_BAUDS) / sizeof(RS485_SCAN_BAUDS[0]);
constexpr uint8_t RS485_SCAN_DEFAULT_MAX_ADDR = 8;

// ── Web server ───────────────────────────────────────────────────────────────
constexpr uint16_t HTTP_PORT = 80;
constexpr uint32_t DASHBOARD_REFRESH_MS = 2000;
