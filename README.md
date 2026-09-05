# Garden Valve Control — ESP32-ACDC-RELAY4-M

Firmware for a 4-channel AC/DC relay board driving garden valves.
Structure and conventions follow the sister project `Garden_IS_ESP32_PLC_14_IO`:
procedural modules, a `WebServer` JSON API, ArduinoOTA + ElegantOTA, and WiFi
credentials kept in source with a gitignored `login.md` as the reference copy.

---

## 1. Hardware

### 1.1 Board

| Item | Value |
| --- | --- |
| Product | ESP32-ACDC-RELAY4-M (HESTORE 100.490.15) |
| Community name | *ESP32_Relay X4 (AC/DC powered)* |
| Module | DOIT `ESP32-32E N4` = ESP32-WROOM-32E |
| Chip | ESP32-D0WD-V3 rev 3.1, dual core 240 MHz, 40 MHz crystal |
| Flash | 4 MB |
| Radio | WiFi 802.11 b/g/n + Bluetooth, u.FL/SMA antenna connector |
| Relays | 4x SPDT, COM / NO / NC screw terminals |
| Supply | 90–250 V AC **or** 7–30 V DC **or** 5 V DC (separate connectors) |
| Size | 93 x 87 mm |
| Extras | 1 programmable button (IO0), 1 programmable LED, 4 relay indicator LEDs, 2x 10x2 GPIO headers |
| This unit's MAC | `04:B2:47:86:14:9C` |

### 1.2 Pin map

| Signal | GPIO | Notes |
| --- | --- | --- |
| Relay 1 | 32 | HIGH = coil energised (driven via darlington array) |
| Relay 2 | 33 | |
| Relay 3 | 25 | |
| Relay 4 | 26 | |
| Status LED | 23 | blinks 2 Hz while connecting, solid = WiFi up |
| Programmable button | 0 | also the boot strapping pin / programming jumper |
| UART0 TX / RX | 1 / 3 | programming header |
| RS485 RX (`RO`) | 16 | UART2, from MAX3485 |
| RS485 TX (`DI`) | 17 | UART2, to MAX3485 |
| RS485 `DE` | 4 | HIGH while transmitting |

The two 10x2 headers break out every remaining ESP32 GPIO for future sensors.

### 1.3 Programming header

```
5V   TX   RX   GND   GND   IO0
```

- **All logic pins are 3.3 V level** — use a 3.3 V-capable USB-TTL adapter.
- `TX`/`RX` on the header are the *board's* pins: connect adapter `RX` -> board `TX`
  and adapter `TX` -> board `RX`.
- There is **no auto-reset circuit** on this header. Download mode is entered by
  jumpering `IO0` to `GND` and then resetting (EN button or power cycle).

> **Power warning.** A USB-TTL adapter cannot supply the four relay coils
> (~70–90 mA each). The board flashes fine on adapter power, but as soon as the
> relays switch it browns out and goes silent on UART. For any functional test,
> power the board from 230 V AC or the dedicated 5 V / 7–30 V DC terminal.
> Never feed 3.3 V into the `5V` pin — the MCU browns out.

> **Mains safety.** Do not leave the USB-TTL adapter attached while the board is
> powered from 230 V AC. Disconnect mains before touching the programming header.

### 1.4 RS485 sensor bus

LY485 temperature/humidity probes on a **MAX3485** TTL-RS485 module (3.3 V part —
at 5 V its `RO` output would overstress the ESP32 input).

| MAX3485 pin | Connects to |
| --- | --- |
| `VCC` | board `3V3` |
| `GND` | board `GND` |
| `RO` | `G16` |
| `DI` | `G17` |
| `DE` | `G4` |
| `RE` | **`GND`** — receiver permanently enabled |
| `A` | sensor **yellow** (485-A) |
| `B` | sensor **green** (485-B) |
| `GND` (bus side) | sensor **black** |

Sensor **red** goes to the board's `5V` pin (probe accepts DC 5–24 V; measured
4.85 V at the sensor, which works). For long buried runs feed it from 12/24 V
instead, to stay clear of the drop.

**Why `RE` is tied to GND rather than switched with `DE`:** it leaves the receiver
always on, so every transmission echoes back into `RO`. `findFrame()` skips the
echo, and in exchange the echo acts as a permanent self-test of the whole analog
path — ESP32 -> `DI` -> driver -> A/B -> receiver -> `RO`. If the echo disappears,
the transceiver or its wiring is at fault, not the sensor.

Bus rules: daisy-chain (never star), twisted pair for A/B, and each sensor needs a
**unique Modbus address** — they all ship as address `1`. Set them one at a time
with `LY485Tools（EN）.exe` before paralleling them.

---

## 2. Software

### 2.1 Toolchain

| Item | Version |
| --- | --- |
| PlatformIO Core | 6.1.19 |
| Platform | `espressif32` 7.0.1 |
| Board definition | `esp32dev` (generic ESP32-WROOM, matches the -32E module) |
| Framework | Arduino ESP32 3.20017 |
| Partition table | platform default (`default.csv`): 2x 1.25 MB OTA app slots |

Current build: ~828 KB of the 1.25 MB app partition, ~49 KB RAM.

### 2.2 Dependencies

| Library | Purpose |
| --- | --- |
| `ayushsharma82/ElegantOTA@^3.0.0` | HTTP firmware updater at `/update` |
| `WebServer` (Arduino core) | JSON API + dashboard on port 80 |
| `ArduinoOTA` (Arduino core) | UDP 3232 updater, espota-compatible |

### 2.3 Project structure

```
platformio.ini          two envs: ota (default) and usb
src/main.cpp            IO layout (OUTPUTS[], SENSORS[]) + setup/loop
src/wifi_network.cpp    WiFi, ArduinoOTA, ElegantOTA, HTTP routes, dashboard
src/logic.cpp           button handling + optional relay click-test
src/sensors.cpp         RS485 Modbus RTU master for the LY485 probes
include/io_config.h     shared types, pin documentation, FIRMWARE_VERSION
include/wifi_network.h  network module API
include/logic.h         logic module API
include/sensors.h       sensor module API
include/secrets.h       WiFi + OTA credentials (gitignored)
scripts/http_ota.py     stdlib-only ElegantOTA uploader used by [env:ota]
docs/manual.md          OCR of the LY485 protocol manual (docling)
login.md                credentials reference (gitignored)
```

### 2.4 Program flow

```
setup()
  Serial.begin(115200)
  pinMode/LOW for every OUTPUTS[] entry     <- relays start de-energised
  setupNetwork()
      connectWiFi()                          <- blocks, blinks status LED
      ArduinoOTA config + begin()
      HTTP routes + ElegantOTA.begin() + server.begin()
  logicSetup()                               <- button pinMode, relays off
  sensorsSetup()                             <- UART2 + DE pin

loop()   every 50 ms
  loopNetwork()  ArduinoOTA.handle() + server.handleClient()
                 + ElegantOTA.loop() + WiFi watchdog (auto reconnect)
  logicLoop()    button poll (+ click-test when RELAY_TEST_ENABLED=1)
  sensorsLoop()  non-blocking Modbus poll, one sensor per 5 s
  delay(50)
```

State is held in module-local `static` variables; there are no classes, no
RTOS tasks, and no blocking calls in `loop()` other than the 50 ms scan delay.

### 2.5 IO layout — adding or removing relays

Edit `OUTPUTS[]` in [src/main.cpp](src/main.cpp):

```cpp
IOOutput OUTPUTS[] = {
    {"relay1", "Relay 1", 32, true},
    ...
    {"status_led", "Status LED", 23, false},
};
```

| Field | Meaning |
| --- | --- |
| `name` | API key used by `/api/control` and `/api/state` |
| `label` | Human-readable name shown on the dashboard |
| `pin` | GPIO number |
| `controllable` | `true` = settable via the API **and** included in the click-test |

`wifiLedIndex()` looks up the entry named `status_led`; rename it and the WiFi
indicator is disabled (returns `-1`), everything else keeps working.

### 2.6 Button and relay click-test

Implemented in [src/logic.cpp](src/logic.cpp).

**Programmable button (GPIO0)** — always active, read with `INPUT_PULLUP`,
pressed = LOW, debounced 50 ms on the falling edge.

- With `RELAY_TEST_ENABLED=0` (current default): a press **toggles the first
  controllable output** in `OUTPUTS[]`, i.e. relay 1. Logged as `[BTN] relay1 -> ON`.
- With `RELAY_TEST_ENABLED=1`: a press drops all relays and restarts the cycle.

> GPIO0 is also the boot strapping pin. Holding the button while the board
> resets puts the ESP32 into download mode instead of running the firmware.

**Click-test** (only when `RELAY_TEST_ENABLED=1`) performs exactly one switching
action per second:

1. **Fill phase** — a randomly chosen relay that is still OFF is switched ON,
   once per second, until all four are ON.
2. **Drain phase** — a randomly chosen relay that is still ON is switched OFF,
   once per second, until all four are OFF.
3. The phase flips and the cycle repeats.

The random pick is a two-pass reservoir over `OUTPUTS[]` seeded from the hardware
RNG (`esp_random()`). Only entries with `controllable = true` take part.

Observed sequence (`/api/state` polled once per second, bit order relay1..relay4):

```
0011 -> 0111 -> 1111 -> 1011 -> 1001 -> 1000 -> 0000 -> 0100 -> 1100 -> 1101 -> 1111
```

### 2.7 RS485 sensors — Modbus RTU

Implemented in [src/sensors.cpp](src/sensors.cpp). Hand-rolled master rather than a
library: it is one frame type, and it keeps full control of the DE turnaround.

Bus parameters: **9600 8N1**, CRC16 (poly `0xA001`, init `0xFFFF`, low byte first).
The probe supports function `0x03` for reads and `0x06` for configuration writes.

| Register | Content | Access |
| --- | --- | --- |
| `0x0000` | Humidity x10, unsigned | read (FC03) |
| `0x0001` | Temperature x10, **signed** two's complement | read (FC03) |
| `0x0100` | Device address (factory default 1) | read/write |
| `0x0101` | Baud rate code | read/write |
| `0x0104` | Temperature correction | read/write |
| `0x0105` | Humidity correction | read/write |

Both values are adjacent, so one transaction fetches both:

```
Request :  01 03 00 00 00 02 C4 0B
Response:  01 03 04 02 30 01 0C FA 11
                    ^^^^^ ^^^^^
                    |     0x010C = 268  -> 26.8 °C
                    0x0230 = 560  -> 56.0 %RH
```

Temperature **must** be parsed as `int16_t` — sub-zero readings arrive as two's
complement, so an unsigned parse turns −9.7 °C into +6543.9 °C.

Adding a probe is a one-line edit to `SENSORS[]` in [src/main.cpp](src/main.cpp):

```cpp
IOSensor SENSORS[] = {
    {"th1", "Temp/Humidity 1", 1},   // name, label, Modbus slave address
};
```

Polling is a non-blocking state machine: one sensor every 5 s, 300 ms response
window, round-robin across `SENSORS[]`. The 50 ms scan cycle never blocks waiting
for a reply. `findFrame()` scans the RX buffer for a CRC-valid response rather than
assuming a fixed offset, which is what lets it ignore the TX echo.

Each reading carries `ok`, `errors`, `last_error` and the raw hex of the last RX
buffer, so a misbehaving bus can be diagnosed entirely over HTTP.

### 2.8 Build flags

| Flag | Default | Effect |
| --- | --- | --- |
| `RELAY_TEST_ENABLED` | `0` | `1` = random one-relay-per-second click-test and the button restarts the cycle; `0` = relays only move on API or button command |

Set it in `[env]` of [platformio.ini](platformio.ini).

### 2.9 Configuration reference

| Setting | Location | Value |
| --- | --- | --- |
| WiFi SSID / password | `include/secrets.h` | `WIFI_SSID` / `WIFI_PASSWORD` |
| ArduinoOTA hostname | `include/secrets.h` | `OTA_HOSTNAME` |
| ArduinoOTA password | `include/secrets.h` | `OTA_PASSWORD` |
| ElegantOTA user / password | `include/secrets.h` | `OTA_HTTP_USER` / `OTA_HTTP_PASS` |
| OTA target IP | `platformio.ini` `[env:ota]` | `192.168.1.109` |
| Serial port | `platformio.ini` `[env:usb]` | `COM6` |
| Firmware version string | `include/io_config.h` | `FIRMWARE_VERSION` |

`include/secrets.h` is gitignored. Copy
[include/secrets.example.h](include/secrets.example.h) to `include/secrets.h` and
fill in the real values before the first build — [scripts/http_ota.py](scripts/http_ota.py)
reads `OTA_HTTP_USER` / `OTA_HTTP_PASS` from that same file, so there is one source
of truth for both firmware and uploader.

---

## 3. HTTP API

Base URL: `http://192.168.1.109`

| Method | Path | Auth | Description |
| --- | --- | --- | --- |
| GET | `/` | none | Dashboard (auto-refresh every 2 s) |
| GET | `/api/state` | none | Firmware version, IP, all output states |
| POST | `/api/control?name=<key>&state=<0/1>` | none | Set one controllable output |
| GET | `/api/rs485?addr=N[&loopback=1]` | none | Bring-up probe — one blocking Modbus read, returns the raw RX bytes |
| GET | `/api/rs485/scan?max=N` | none | Sweeps every baud rate against addresses 1..N (blocking, ~12 s) |
| GET | `/update` | Basic | ElegantOTA web updater |
| GET | `/ota/start?mode=firmware&hash=<md5>` | Basic | ElegantOTA handshake |
| POST | `/ota/upload` | Basic | ElegantOTA multipart firmware upload |

`GET /api/state` response:

```json
{
  "firmware": "v0.5",
  "ip": "192.168.1.109",
  "outputs": [
    { "name": "relay1", "label": "Relay 1", "state": false, "controllable": true },
    { "name": "status_led", "label": "Status LED", "state": true, "controllable": false }
  ],
  "sensors": [
    {
      "name": "th1", "label": "Temp/Humidity 1", "address": 1, "ok": true,
      "temperature_c": 26.6, "humidity_pct": 55.9,
      "errors": 0, "last_error": "",
      "raw": "01 03 00 00 00 02 C4 0B 01 03 04 02 2F 01 0A 4B D5",
      "age_s": 0
    }
  ]
}
```

`POST /api/control` returns `{"ok":true}`, or `403` if the output is not
controllable, `404` if the name is unknown, `400` on missing parameters.

PowerShell examples:

```powershell
(Invoke-WebRequest http://192.168.1.109/api/state -UseBasicParsing).Content
Invoke-WebRequest "http://192.168.1.109/api/control?name=relay1&state=1" -Method POST -UseBasicParsing
(Invoke-WebRequest "http://192.168.1.109/api/rs485?addr=1" -UseBasicParsing).Content
```

---

## 4. How-to — build & upload

### 4.1 First-time / recovery flash over USB-TTL

1. Disconnect mains power from the board.
2. Wire the adapter to the programming header: `TX`->`RX`, `RX`->`TX`, `GND`->`GND`,
   `5V`->`5V` (adapter must output 5 V on that pin, 3.3 V logic on TX/RX).
3. Fit the **IO0 -> GND jumper**.
4. Press **EN** (or replug) to enter download mode.
5. Flash:
   ```powershell
   pio run -e usb -t upload
   ```
6. **Remove the IO0 jumper**, disconnect the adapter, and power the board from
   230 V AC (or the DC terminal).

While the jumper is on, the board reboots straight back into download mode and
prints nothing on UART — that is expected, not a fault.

### 4.2 Normal upload — OTA

`[env:ota]` is the default environment, so:

```powershell
pio run -t upload
```

Expected output:

```
[OTA] firmware.bin  827 KB  ->  http://192.168.1.109
[OTA] Step 1: GET /ota/start ...      -> HTTP 200  OK
[OTA] Step 2: POST /ota/upload ...    -> HTTP 200  OK
[OTA] Device online after 4s -- upload successful!
```

Verify the new build actually took over by bumping `FIRMWARE_VERSION` in
[include/io_config.h](include/io_config.h) and re-reading `/api/state`.

### 4.3 Serial monitor

```powershell
pio device monitor -e usb
```

Only meaningful with the USB-TTL adapter attached — i.e. bench work, not while
the board runs on mains.

### 4.4 Finding the device IP again

The DHCP lease may change. Look it up by MAC:

```powershell
1..254 | ForEach-Object -Parallel { Test-Connection -Count 1 -TimeoutSeconds 1 -Quiet -TargetName "192.168.1.$_" | Out-Null } -ThrottleLimit 64
arp -a | Select-String "04-b2-47"
```

Then update `upload_port` in `[env:ota]`.

---

## 5. OTA

Three independent update paths are available. All of them write to the inactive
OTA app slot and swap on reboot, so a failed upload leaves the running firmware
intact.

### 5.1 ElegantOTA over HTTP (used by `pio run -t upload`)

- Transport: HTTP on port 80, Basic auth from `include/secrets.h`.
- Driver: [scripts/http_ota.py](scripts/http_ota.py) — Python stdlib only, no pip
  packages. PlatformIO calls it as `python scripts/http_ota.py <ip> <firmware.bin>`.
- Flow:
  1. `GET /ota/start?mode=firmware&hash=<md5>` — arms the updater.
  2. `POST /ota/upload` — multipart body, field name `firmware`.
  3. Poll `GET /` every 2 s (up to 80 s) until the device answers `200` again.
- Chosen as the default because it needs no inbound firewall rule on the PC,
  unlike the UDP-based espota flow.

### 5.2 ElegantOTA web UI

Browse to `http://192.168.1.109/update`, log in, and drop
`.pio/build/ota/firmware.bin` on the page. Useful from a phone or another machine.

### 5.3 ArduinoOTA (espota)

Also active: hostname `Garden-Valve-Relay4`, UDP port 3232, password in `include/secrets.h`.
Use it by switching `[env:ota]` to `upload_protocol = espota`. Requires the PC
firewall to allow the inbound UDP handshake.

### 5.4 Safety behaviour

`ArduinoOTA.onStart()` forces every `controllable` output OFF before the flash
begins, so no valve can be left energised by an interrupted update. Progress is
printed to serial as `OTA: <n>%`, errors as `OTA Error [<code>]`.

---

## 6. Troubleshooting

| Symptom | Cause | Fix |
| --- | --- | --- |
| `Failed to connect: No serial data received` | Board not in download mode | Fit IO0 jumper, press EN, retry |
| Upload OK, then no serial output at all | IO0 jumper still fitted — board re-entered download mode | Remove jumper, press EN |
| Board dies as soon as relays switch | USB-TTL 5 V rail cannot feed the coils | Power from 230 V AC or the DC terminal |
| Status LED keeps blinking | WiFi not associating | Check SSID/password in `src/wifi_network.cpp`, confirm 2.4 GHz band and signal |
| `[OTA] FAILED at /ota/start` | Wrong IP, device offline, or wrong Basic auth | Re-check `upload_port`, ping the device, verify credentials |
| OTA succeeds but version unchanged | `FIRMWARE_VERSION` not bumped | Bump the version string and re-read `/api/state` |
| Serial garbage | Wrong baud rate | Monitor at 115200 |
| RS485: `/api/rs485` returns nothing at all | Receiver disabled or `RO`/`DI` swapped | With `RE` tied to GND you must always see the 8-byte echo; if not, the module wiring is wrong |
| RS485: echo only, never a reply | Sensor not answering — A/B polarity, broken A/B wire, wrong address, or no sensor power | Echo does **not** prove polarity or continuity. Swap yellow/green, check the pair end-to-end, run `/api/rs485/scan` |
| RS485: one nonsense frame at power-up | Sensor booting mid-transaction | Harmless; CRC rejects it and the next poll is clean |
| Temperature reads ~+6500 °C | Register parsed unsigned | Must be `int16_t` — sub-zero values are two's complement |

---

## 7. Status & next steps

Working today: WiFi station with reconnect watchdog, dashboard, JSON API, OTA over
three transports, button-toggled relay 1, and one LY485 temperature/humidity probe
polling cleanly over RS485 (0 errors over sustained runs).

Next up:

- Set the second probe to address `2` and add it to `SENSORS[]`.
- Sort the 24 VDC -> 24 VAC supply for the Hunter PGV valves (a 24 VAC transformer
  is the plan).
- Replace the click-test in [src/logic.cpp](src/logic.cpp) with the real valve
  schedule/interlock logic, mirroring the rung style used in
  `Garden_IS_ESP32_PLC_14_IO/src/logic.cpp`.
