# Garden Valve Control

Irrigation controller for a 4-channel AC/DC relay board: WiFi, a web dashboard, a
JSON API, over-the-air updates, and two RS485 temperature/humidity probes.

Built in the style of its sister project `Garden_IS_ESP32_PLC_14_IO` — procedural
modules, no RTOS tasks, no classes, all state in module-local statics.

```
                    ┌──────────────────────────────┐
   230 V AC ───────►│  ESP32-ACDC-RELAY4-M         │
                    │                              │
   WiFi  ◄─────────►│  ESP32-WROOM-32E             │
                    │                              │
                    │  4x SPDT relay ──────────────┼──► valves
                    │  UART2 ── MAX3485 ───────────┼──► RS485: Sensor-A, Sensor-B
                    └──────────────────────────────┘
```

**Contents** — [Quick start](#quick-start) · [Hardware](#hardware) ·
[Firmware](#firmware) · [HTTP API](#http-api) · [Build &amp; upload](#build--upload) ·
[Commissioning](#commissioning-rs485) · [Troubleshooting](#troubleshooting)

---

## Quick start

```powershell
# 1. credentials
Copy-Item include/secrets.example.h include/secrets.h   # then fill it in

# 2. build and flash over the air
pio run -t upload

# 3. check it
(Invoke-WebRequest http://192.168.1.109/api/state -UseBasicParsing).Content
```

Dashboard: `http://192.168.1.109/`

---

## Hardware

### Board

| Item | Value |
| --- | --- |
| Product | ESP32-ACDC-RELAY4-M (HESTORE 100.490.15) |
| Community name | *ESP32_Relay X4 (AC/DC powered)* |
| Module | DOIT `ESP32-32E N4` = ESP32-WROOM-32E |
| Chip | ESP32-D0WD-V3 rev 3.1, dual core 240 MHz, 40 MHz crystal |
| Flash | 4 MB |
| Relays | 4x SPDT, COM / NO / NC screw terminals |
| Supply | 90–250 V AC **or** 7–30 V DC **or** 5 V DC (separate connectors) |
| Extras | programmable button, programmable LED, 4 relay LEDs, 2x 10x2 GPIO headers |
| MAC | `04:B2:47:86:14:9C` |

### Pin map

Defined once in [include/config.h](include/config.h).

| Signal | GPIO | Notes |
| --- | --- | --- |
| Valve 1–4 | 32, 33, 25, 26 | HIGH = coil energised (darlington driver) |
| Status LED | 23 | 2 Hz blink while connecting, solid = WiFi up |
| Button | 0 | also the boot strapping pin |
| RS485 `RO` → RX | 16 | UART2 |
| RS485 `DI` ← TX | 17 | UART2 |
| RS485 `DE` | 4 | HIGH while transmitting |
| UART0 TX / RX | 1, 3 | programming header / console |

### Programming header

```
5V   TX   RX   GND   GND   IO0
```

3.3 V logic. Adapter `RX` → board `TX`, adapter `TX` → board `RX`. There is **no
auto-reset circuit**: download mode needs `IO0` jumpered to `GND` followed by a
reset.

> [!WARNING]
> A USB-TTL adapter cannot supply the four relay coils (~70–90 mA each). The board
> flashes fine on adapter power, but browns out the moment a relay switches. Power
> it from 230 V AC or the DC terminal for any functional test, and never feed
> 3.3 V into the `5V` pin.

> [!CAUTION]
> Do not leave the USB-TTL adapter attached while the board runs on 230 V AC.
> Disconnect mains before touching the programming header.

### RS485 sensor bus

LY485 probes on a **MAX3485** module — a 3.3 V part, because at 5 V its `RO`
output would overstress the ESP32 input.

| MAX3485 | Connects to |
| --- | --- |
| `VCC` / `GND` | board `3V3` / `GND` |
| `RO` / `DI` | `G16` / `G17` |
| `DE` | `G4` |
| `RE` | **`GND`** — receiver permanently enabled |
| `A` / `B` | sensor **yellow** / **green** |
| `GND` (bus) | sensor **black** |

Sensor **red** → board `5V` (probes accept DC 5–24 V; 4.85 V measured at the
sensor works). Feed long buried runs from 12/24 V instead, to stay clear of the
voltage drop.

> [!NOTE]
> **Why `RE` is grounded instead of switched with `DE`.** The receiver stays on
> during transmission, so every request echoes back into `RO`. `findFrame()`
> skips the echo, and in return the echo becomes a permanent self-test of the
> whole analog path: ESP32 → `DI` → driver → A/B → receiver → `RO`. If the echo
> disappears, the fault is the transceiver or its wiring, not the sensor.
> Diagnosing a silent bus without that signal is far harder.

Daisy-chain the bus (never star) and use a twisted pair for A/B. Each probe needs
a **unique Modbus address** — see [Commissioning](#commissioning-rs485).

### Valves

The intended actuators are Hunter PGV-100-G-B, which have **24 V AC** solenoids
(370 mA inrush, 210 mA holding at 50 Hz, minimum 19 V AC). The site supply is
24 V DC, so a 24 V AC transformer is still to be fitted — a DC coil would draw
~0.9 A through the ~27 Ω winding and burn out.

Wire the field side as COM + NO, so every valve is closed whenever the board is
off, rebooting or mid-update.

---

## Firmware

### Module layout

Each `.cpp`/`.h` pair is one subsystem, split by responsibility rather than by
convenience:

```
src/main.cpp            hardware inventory (OUTPUTS[], SENSORS[]) + setup/loop
src/valve_control.cpp   relay switching, button, automation rungs
src/sensors.cpp         RS485 Modbus RTU master
src/wifi_network.cpp    WiFi association + ArduinoOTA (UDP)
src/web.cpp             dashboard, JSON API, ElegantOTA (HTTP)

include/config.h        every tunable constant in the project
include/io_config.h     IOOutput / IOSensor types, extern tables
include/secrets.h       WiFi + OTA credentials (gitignored)

scripts/http_ota.py     stdlib-only ElegantOTA uploader
docs/manual.md          OCR of the LY485 protocol manual
```

OTA is split by transport: `wifi_network.cpp` owns the UDP-based ArduinoOTA,
`web.cpp` owns the HTTP-based ElegantOTA, alongside the server they share.

### Program flow

```
setup()
  valveControlSetup()    all relays LOW first — a reset must never leave a
                         valve energised while the network comes up
  wifiSetup()            blocks until associated, then starts ArduinoOTA
  webSetup()             routes + ElegantOTA + server.begin()
  sensorsSetup()         UART2 + DE pin

loop()   every SCAN_CYCLE_MS (50 ms)
  wifiLoop()             ArduinoOTA + association watchdog
  webLoop()              HTTP requests + ElegantOTA
  valveControlLoop()     button + automation rungs
  sensorsLoop()          non-blocking Modbus poll, one probe per 5 s
```

Nothing in `loop()` blocks except the final delay. State lives in module-local
`static` variables; there are no classes and no RTOS tasks.

### Adding hardware

Both tables live in [src/main.cpp](src/main.cpp) and are the only thing that
needs editing — the API, dashboard, button and poller all iterate them.

```cpp
IOOutput OUTPUTS[] = {
    {"relay1", "Valve 1", RELAY_1_PIN, true},   // name, label, pin, controllable
    {"status_led", "Status LED", STATUS_LED_PIN, false},
};

IOSensor SENSORS[] = {
    {"sensor_a", "Sensor-A", 1},                // name, label, Modbus address
    {"sensor_b", "Sensor-B", 2},
};
```

`controllable = false` keeps an entry visible on the dashboard while excluding it
from the API, the button and the click-test — used for the status LED.

### Valve control

Every relay write goes through `valveSet()`. Interlocks added there apply to the
API, the button and the automation at once — the natural home for a future
"one zone at a time" rule or a minimum gap between switching operations.

**Button (GPIO0)** — debounced 50 ms falling edge, always active:

- normally: toggles the first controllable output (Valve 1)
- with `RELAY_TEST_ENABLED=1`: drops all relays and restarts the click-test

> [!WARNING]
> GPIO0 is the boot strapping pin. Holding the button while the board resets
> enters serial download mode instead of running the firmware.

**Click-test** (`RELAY_TEST_ENABLED=1`) exercises the wiring: one switching
action per second, all relays ON in random order, then OFF in random order, then
repeat. Turn it off before connecting real valves — switching a 1" valve once a
second hammers the pipework.

Sensor-driven rules go in `runAutomation()` in
[src/valve_control.cpp](src/valve_control.cpp). Check `reading.valid` before
acting: a probe that has dropped off the bus keeps its last values, and deciding
on stale humidity would leave a valve open.

### RS485 sensors

A hand-rolled Modbus RTU master rather than a library — it is one frame type, and
it keeps full control of the DE turnaround. Bus is **9600 8N1**, CRC16
(poly `0xA001`, seed `0xFFFF`, low byte first).

| Register | Content | Access |
| --- | --- | --- |
| `0x0000` | Humidity ×10, unsigned | read (FC03) |
| `0x0001` | Temperature ×10, **signed** | read (FC03) |
| `0x0100` | Device address | read/write (FC06) |
| `0x0101` | Baud rate code | read/write |
| `0x0104` / `0x0105` | Temperature / humidity correction | read/write |

Humidity and temperature are adjacent, so one transaction fetches both:

```
Request :  01 03 00 00 00 02 C4 0B
Response:  01 03 04 02 30 01 0C FA 11
                    ^^^^^ ^^^^^
                    |     0x010C = 268  ->  26.8 °C
                    0x0230 = 560  ->  56.0 %RH
```

> [!IMPORTANT]
> Temperature must be parsed as `int16_t`. Sub-zero readings arrive as two's
> complement, so an unsigned parse turns −9.7 °C into +6543.9 °C.

Polling is a non-blocking state machine: one probe every 5 s, 300 ms response
window, round-robin. `findFrame()` scans the buffer for a frame with a matching
address, function code, byte count *and* CRC rather than assuming a fixed offset —
that is what lets it ignore the TX echo and any turnaround noise.

Every reading carries `ok`, `errors`, `last_error` and the raw hex of the last RX
buffer, so a misbehaving bus can be diagnosed entirely over HTTP.

### Configuration

All constants live in [include/config.h](include/config.h) — pins, timings,
Modbus registers, web settings. Nothing else in the project holds a magic number.

| Setting | Location |
| --- | --- |
| WiFi SSID / password | `include/secrets.h` → `WIFI_SSID`, `WIFI_PASSWORD` |
| ArduinoOTA hostname / password | `include/secrets.h` → `OTA_HOSTNAME`, `OTA_PASSWORD` |
| ElegantOTA user / password | `include/secrets.h` → `OTA_HTTP_USER`, `OTA_HTTP_PASS` |
| OTA target IP | `platformio.ini` `[env:ota]` → `upload_port` |
| Serial port | `platformio.ini` `[env:usb]` → `upload_port` |
| Firmware version | `include/config.h` → `FIRMWARE_VERSION` |

`include/secrets.h` is gitignored. Copy
[include/secrets.example.h](include/secrets.example.h) and fill it in before the
first build — [scripts/http_ota.py](scripts/http_ota.py) reads `OTA_HTTP_USER` and
`OTA_HTTP_PASS` from that same file, so firmware and uploader share one source of
truth.

**Build flags** ([platformio.ini](platformio.ini)):

| Flag | Default | Effect |
| --- | --- | --- |
| `RELAY_TEST_ENABLED` | `0` | `1` = random one-relay-per-second click-test |

### Toolchain

| Item | Version |
| --- | --- |
| PlatformIO Core | 6.1.19 |
| Platform | `espressif32` 7.0.1 |
| Board | `esp32dev` (generic ESP32-WROOM, matches the -32E) |
| Framework | Arduino ESP32 3.20017 |
| Partitions | default `default.csv` — 2x 1.25 MB OTA app slots |
| Dependency | `ayushsharma82/ElegantOTA@^3.0.0` |

Current build: ~848 KB of the 1.25 MB app slot, ~50 KB RAM.

---

## HTTP API

Base URL `http://192.168.1.109`.

| Method | Path | Description |
| --- | --- | --- |
| GET | `/` | Dashboard, refreshes every 2 s |
| GET | `/api/state` | Firmware, IP, relays and sensor readings |
| POST | `/api/control?name=<key>&state=<0\|1>` | Switch one controllable output |
| GET | `/api/rs485?addr=N[&loopback=1]` | Bus probe, returns raw bytes |
| GET | `/api/rs485/scan?max=N` | Baud/address sweep (blocking, ~12 s) |
| POST | `/api/rs485/setaddr?from=A&to=B` | Readdress a probe — one sensor only |
| GET | `/update` | ElegantOTA (Basic auth) |

`GET /api/state`:

```json
{
  "firmware": "v0.8",
  "ip": "192.168.1.109",
  "outputs": [
    { "name": "relay1", "label": "Valve 1", "state": false, "controllable": true },
    { "name": "status_led", "label": "Status LED", "state": true, "controllable": false }
  ],
  "sensors": [
    {
      "name": "sensor_a", "label": "Sensor-A", "address": 1, "ok": true,
      "temperature_c": 27.5, "humidity_pct": 48.8,
      "errors": 0, "last_error": "",
      "raw": "01 03 00 00 00 02 C4 0B 01 03 04 01 E8 01 13 4B D5",
      "age_s": 0
    }
  ]
}
```

Temperature and humidity are **omitted** when `ok` is false, so a client can never
mistake a stale reading for a fresh one.

`POST /api/control` returns `{"ok":true}`, or `400` (missing arguments), `403`
(not controllable), `404` (unknown name).

```powershell
(Invoke-WebRequest http://192.168.1.109/api/state -UseBasicParsing).Content
Invoke-WebRequest "http://192.168.1.109/api/control?name=relay1&state=1" -Method POST -UseBasicParsing
(Invoke-WebRequest "http://192.168.1.109/api/rs485?addr=1" -UseBasicParsing).Content
```

---

## Build &amp; upload

### OTA — the normal path

`[env:ota]` is the default environment:

```powershell
pio run -t upload
```

```
[OTA] firmware.bin  828 KB  ->  http://192.168.1.109
[OTA] Step 1: GET /ota/start ...      -> HTTP 200  OK
[OTA] Step 2: POST /ota/upload ...    -> HTTP 200  OK
[OTA] Device online after 4s -- upload successful!
```

Bump `FIRMWARE_VERSION` in [include/config.h](include/config.h) and re-read
`/api/state` to confirm the new build actually took over.

Three update paths exist, all writing to the inactive OTA slot and swapping on
reboot, so a failed upload leaves the running firmware intact:

1. **ElegantOTA over HTTP** — the default. [scripts/http_ota.py](scripts/http_ota.py)
   (Python stdlib only) does `GET /ota/start` → `POST /ota/upload` → polls `GET /`
   until the device answers again. Chosen because it needs no inbound firewall
   rule, unlike the UDP espota handshake.
2. **ElegantOTA web UI** — drop `.pio/build/ota/firmware.bin` at `/update`.
   Handy from a phone.
3. **ArduinoOTA / espota** — also active on UDP 3232. Switch `[env:ota]` to
   `upload_protocol = espota`.

`ArduinoOTA.onStart()` forces every valve off before the flash, so no zone can be
left energised by an interrupted update.

### USB — first flash and recovery

1. Disconnect mains.
2. Wire the adapter: `TX`↔`RX`, `RX`↔`TX`, `GND`↔`GND`, `5V`↔`5V`.
3. Fit the **IO0 → GND jumper**.
4. Press **EN** to enter download mode.
5. `pio run -e usb -t upload`
6. Remove the jumper, disconnect the adapter, restore mains power.

While the jumper is fitted the board reboots straight back into download mode and
prints nothing on UART — expected, not a fault.

```powershell
pio device monitor -e usb     # 115200, bench work only
```

### Finding the device again

DHCP leases change. Look it up by MAC:

```powershell
1..254 | ForEach-Object -Parallel { Test-Connection -Count 1 -TimeoutSeconds 1 -Quiet -TargetName "192.168.1.$_" | Out-Null } -ThrottleLimit 64
arp -a | Select-String "04-b2-47"
```

Then update `upload_port` in `[env:ota]`.

---

## Commissioning (RS485)

Every probe ships as address `1`, so a second one must be moved before they can
share the pair. No USB-RS485 adapter or vendor tool is needed.

1. Disconnect the **A/B wires of every other sensor** — power may stay on. The
   write reaches whoever answers at `from`, so otherwise they all take the new
   address.
2. Confirm the target is alone — one clean frame, no garbage:
   ```powershell
   (Invoke-WebRequest "http://192.168.1.109/api/rs485?addr=1" -UseBasicParsing).Content
   ```
3. Readdress it:
   ```powershell
   Invoke-WebRequest "http://192.168.1.109/api/rs485/setaddr?from=1&to=2" -Method POST -UseBasicParsing
   ```
4. A successful write echoes the request back:
   ```
   01 06 01 00 00 02 09 F7   <- our TX echo
   01 06 01 00 00 02 09 F7   <- sensor acknowledgment
   ```
5. Verify at `addr=2`, reconnect the others, add the probe to `SENSORS[]`.

Two probes on one address produce **corrupted** frames rather than silence — e.g.
`E0 30 78 FD 60 EC FF`, both drivers fighting on the pair. That fails CRC and
surfaces as `noframe`.

If nothing answers at all, sweep every baud rate and address:

```powershell
$hits = (Invoke-WebRequest "http://192.168.1.109/api/rs485/scan?max=8" -UseBasicParsing -TimeoutSec 90).Content | ConvertFrom-Json
$hits | Where-Object { $_.raw.Length -gt 23 } | Format-Table baud,addr,raw -AutoSize
```

The filter drops echo-only rows: our own 8-byte frame is 23 characters of hex.

---

## Troubleshooting

### Upload

| Symptom | Cause | Fix |
| --- | --- | --- |
| `Failed to connect: No serial data received` | Not in download mode | Fit the IO0 jumper, press EN, retry |
| Upload OK, then no serial output at all | IO0 jumper still fitted | Remove it, press EN |
| Board dies when a relay switches | USB-TTL 5 V cannot feed the coils | Power from mains or the DC terminal |
| OTA succeeds but version unchanged | `FIRMWARE_VERSION` not bumped | Bump it, re-read `/api/state` |
| `[OTA] FAILED at /ota/start` | Wrong IP, offline, or bad auth | Check `upload_port`, ping it, verify `secrets.h` |

### RS485

Work down this list — each step rules out one layer.

| Symptom | Cause | Fix |
| --- | --- | --- |
| `/api/rs485?loopback=1` returns nothing | UART or frame building is broken | The internal loopback bypasses all wiring; a failure here is firmware, not hardware |
| Loopback fine, but normal probe returns nothing | Receiver disabled, or `RO`/`DI` swapped | With `RE` on GND the 8-byte echo must always appear |
| Echo only, never a reply | Sensor not answering | Echo proves **neither** A/B polarity **nor** continuity — our driver and receiver agree regardless. Swap yellow/green, check the pair end to end, run the scan |
| Intermittent garbage like `E0 30 78 FD` | Two probes sharing an address | Isolate and readdress, see [Commissioning](#commissioning-rs485) |
| One nonsense frame at power-up | Probe booting mid-transaction | Harmless; CRC rejects it and the next poll is clean |
| Temperature reads ~+6500 °C | Register parsed unsigned | Must be `int16_t` |
| Spurious bytes on an idle bus | No fail-safe bias | ~680 Ω from `A` to 3.3 V and `B` to GND |

### Other

| Symptom | Cause | Fix |
| --- | --- | --- |
| Status LED keeps blinking | Not associating | Check `secrets.h`, confirm 2.4 GHz and signal |
| Serial garbage | Wrong baud | Monitor at 115200 |

---

## Status

Working: WiFi with reconnect watchdog, dashboard, JSON API, OTA over three
transports, button-toggled Valve 1, and two probes (`Sensor-A`, `Sensor-B`)
polling cleanly over RS485.

Next:

- Fit the 24 V AC transformer for the Hunter PGV valves.
- Implement the irrigation rules in `runAutomation()`
  ([src/valve_control.cpp](src/valve_control.cpp)), mirroring the rung style of
  `Garden_IS_ESP32_PLC_14_IO/src/logic.cpp`.
