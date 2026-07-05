# Fujitsu ASTG34KMTC — FujitsuAC firmware for the nathansags ESP32-C3 board

Firmware build project to bring the Fujitsu ASTG34KMTC into Home Assistant with
full local control, via the unit's USB-A "WiFi dongle" port, using the
nathansags ESP32-C3 board running [Benas09/FujitsuAC](https://github.com/Benas09/FujitsuAC)
(a.k.a. *faircon* — replaces the UTY-TFSX\* dongles, MQTT + HA autodiscovery,
fully local).

## Headline result of the source audit: the C3 port already exists upstream

The Rev 2 handoff doc assumed FujitsuAC (then v1.1.10) needed porting to the
ESP32-C3. Upstream has since moved to **v1.3.16** (June 2026) and the work is
already done:

- `examples/Controller/Controller.ino` has a `CONFIG_IDF_TARGET_ESP32C3` block
  that selects **RX GPIO20 / TX GPIO21 on UART1** — exactly the pins we
  inferred for the board's CN1 header.
- On the C3, the status LEDs and reset button default to **disabled** (`-1`),
  so the classic-ESP32 LED pins 18/19 (this board's native USB pins) are never
  touched. No conflict with USB1 flashing/debug.
- A prebuilt `fw/esp32c3.bin` now ships in the repo, and the firmware's
  built-in network updater explicitly serves the `esp32c3` image to C3 chips.
  Verified with `esptool image-info`: genuine ESP32-C3 app image, 4MB flash,
  DIO — matching the ESP32-C3-WROOM-02 on this board.

`src/main.cpp` here is the upstream Controller example with the C3 pin choices
made explicit. It is **functionally identical to the upstream C3 defaults**,
deliberately: the dongle's one-click network update pulls upstream's
`esp32c3.bin`, and because our config matches it, an OTA update never changes
pin behaviour.

## Other audit findings

- **The AC-side UART is inverted logic, 9600 baud 8N1** (`src/Uart.cpp` calls
  `uart_set_line_inverse` on both TX and RX). An inverted UART idles LOW.
  This resolves the "slightly odd" multimeter readings in the handoff doc —
  they in fact corroborate Benas's pinout exactly:
  - USB pin 2 (AC TX): measured 0 V idle ✓ inverted-UART idle is low.
  - USB pin 3 (AC RX): measured 5.0 V ✓ input pull-up with nothing driving it.
- No Xtensa-specific code, no dual-core assumptions, no core pinning — plain
  Arduino `setup()`/`loop()` with the ESP-IDF UART driver. Clean for the
  single-core RISC-V C3.
- Requires **Arduino-ESP32 core 3.x** (`NetworkClient.h`, `ledcAttach`, …).
  Official PlatformIO ships core 2.x only, hence the
  [pioarduino](https://github.com/pioarduino/platform-espressif32) platform in
  `platformio.ini`.
- **No serial-console logging exists in this firmware.** All diagnostics —
  status, errors, and every UART frame sent/received (as hex) — are published
  to MQTT under `fujitsu/<uniqueId>/…`. Bench observability is via
  `mosquitto_sub`, not a USB serial monitor.
- The ESP initiates the handshake (sends Init1, retries after a 200 ms
  "No response" timeout), so the dongle produces observable traffic even with
  nothing answering.
- Upstream has no LICENSE file ("All rights reserved" headers), so the library
  is referenced as a dependency pinned to the audited commit
  (`0f3c2d5`, v1.3.16) rather than vendored here.
- Default 4MB partition scheme: the app is ~1.22 MB of the 1.28 MB OTA slot
  (~95%). Fine today; if a future version outgrows it, set
  `board_build.partitions = min_spiffs.csv`. (Upstream's own OTA images have
  the same constraint, so they'll hit it first.)

## ⚠️ New risk found: level-shifter polarity

Because the firmware *already* inverts TX/RX at the ESP pins, the board's
CN1 level shifter must be **non-inverting** end-to-end. Single-transistor
shifter stages (the Q4 area on this board) often invert; two in series don't.
Nathan's LIN-bus customers prove the CN1 data path works, but not its polarity
for this protocol.

**The CN1 loopback test cannot catch this** — an inversion in the TX path is
cancelled by the same inversion in the RX path, so loopback passes either way.
The decisive bench check is a multimeter: with the firmware running and the
board *not* connected to the AC, **CN1 Tx should idle at ~0 V** (inverted-UART
idle, matching the AC TX's measured 0 V idle). If CN1 Tx idles at ~5 V, the
shifter inverts — fix by removing the `uart_set_line_inverse` call (or
changing it to `UART_SIGNAL_INV_DISABLE`) in a locally patched copy of
`Uart.cpp`, and disable the network updater's auto-update before deploying.

## Build & flash (on the Mac — not in a cloud session)

```sh
brew install platformio        # or: pipx install platformio
cd fujitsu-ac
pio run                        # compile only
pio run -t upload              # first flash: writes bootloader + partitions + app
```

- Hold **SW2 (PRGM)** while plugging in USB1 to enter the bootloader.
- `upload_speed = 460800` is already set (higher rates fail on macOS).
- The board currently runs Nathan's ESPHome halcyon firmware. Do a clean
  start: `pio pkg exec -- esptool.py --chip esp32c3 erase_flash` before the
  first upload (ESPHome's partition table and NVS layout differ; Nathan's
  factory .bin is in Tom's files if the board ever needs to be restored).
- ⛔ **Ground-loop rule stands: never connect USB1 and the AC cable at the
  same time.** Flash fully disconnected from the unit.
- Recovery/alternative app image (same config as this build):
  `https://raw.githubusercontent.com/Benas09/FujitsuAC/master/fw/esp32c3.bin`
  — app-only; flash at `0x10000` on a board that already has the Arduino
  bootloader + partition table from a previous `pio run -t upload`.

## Configure credentials

1. On first boot the dongle creates an open AP named `faircon-<uniqueId>`.
2. Join it and open `http://192.168.1.1`.
3. Enter WiFi credentials, MQTT host `192.168.50.184`, port `1883`, the
   Mosquitto add-on username/password, a device name, and a device password
   (needed later for ArduinoOTA).
4. It reboots, joins the LAN, connects to Mosquitto, and appears in Home
   Assistant automatically via MQTT autodiscovery (climate + sensors +
   diagnostics).

## Phase 2 — bench verification (USB1 power only; no 12 V supply needed)

The level shifter runs off the board's own 5 V rail, so everything below works
powered from USB1. **H2 jumper stays at 12 V** (supply-select only — never
move it to 5 V while the AC cable is attached).

1. Flash, configure credentials, confirm the device connects to Mosquitto:
   `mosquitto_sub -h 192.168.50.184 -t 'fujitsu/#' -v`
2. Expect `…/debug/status MqttBridge started`, then repeating
   `Init1 Send` / `No response for 200 ms` — that's healthy with no AC
   attached, and proves the firmware is driving the UART.
3. **Pin-inference test:** jumper CN1 Rx↔Tx. The looped-back Init1 frame
   should now show up on the `…/debug/received` topic (hex bytes echoing the
   `…/debug/send` frame, likely followed by an "Unexpected response" error —
   that's success). This confirms CN1 really is GPIO20/21 through the shifter.
4. **Polarity test (loopback can't detect this):** remove the jumper, measure
   CN1 Tx to G with the firmware running — must idle ~0 V (see risk section
   above). While there, confirm CN1 pin voltages never exceed ~5 V.

## Phase 3 — live test

1. Disconnect USB1 completely. Connect the built cable: CN1 → AC USB port
   (wiring per the handoff doc: V+/yellow=12 V, Rx/white=AC TX,
   Tx/red=AC RX, G/black=GND).
2. Watch `mosquitto_sub -h 192.168.50.184 -t 'fujitsu/#' -v`.
3. Success = `Init1 Send` → `Init2 Send` → `Running`, then registry/status
   frames. The climate entity appears in HA via autodiscovery.
4. Test mode/setpoint/fan from HA and from the IR remote (upstream confirms
   IR and dongle coexist; wall controller UTY-RLRY too).

## Fallbacks if Init1 gets no answer

- All raw traffic is already logged as hex on `…/debug/send` /
  `…/debug/received` — capture with
  `mosquitto_sub … >> log.txt` and post to the
  [HA community thread](https://community.home-assistant.io/t/fujitsuac-integration/914288)
  and/or a GitHub issue; Benas actively adds device support from user logs.
  (The repo also ships a dedicated `examples/Sniffer` sketch for
  man-in-the-middle captures with a real dongle, if it ever comes to that.)
- Re-check the polarity risk above before concluding the protocol is wrong.
- Pragmatic fallback: buy a ready-made tested dongle from Benas (contact via
  repo/email); proven on multiple Fujitsu splits, including the Oceania
  UTY-TFSXF3 the ASTG34KMTC uses.
- Last resort: official anywAiR UTY-NXT-BOX (~NZ$240) + FGLair cloud.

## Layout

- `platformio.ini` — pioarduino platform, `esp32-c3-devkitm-1` board, pinned
  library deps.
- `src/main.cpp` — Controller sketch with the nathansags CN1 pin mapping.
