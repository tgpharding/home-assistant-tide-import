# Fujitsu ASTG34KMTC — Home Assistant Integration Project (Rev 3)

> **Rev 3 note (2026-07-06):** Supersedes Rev 2. Phase 1 (audit & port) is
> complete — and the central assumption of Rev 2 turned out to be obsolete:
> **no port was needed.** Upstream `Benas09/FujitsuAC` moved from v1.1.10 to
> v1.3.16 and now supports the ESP32-C3 natively, with UART pins that match
> the nathansags board's CN1 header exactly. The project is now at the
> bench-verification stage. Build project and detailed procedures live in
> [`README.md`](README.md) in this directory.

## What changed since Rev 2

| Rev 2 said | Rev 3 reality |
|---|---|
| FujitsuAC v1.1.10; C3 needs porting from source | v1.3.16 (June 2026); `CONFIG_IDF_TARGET_ESP32C3` block already selects **RX GPIO20 / TX GPIO21 on UART1**, prebuilt `fw/esp32c3.bin` ships in-repo (verified: genuine C3 image, 4MB DIO) |
| LEDs on 18/19 must be disabled/remapped for C3 | Already disabled by default on C3 upstream; USB pins never touched |
| AC TX idling at 0 V is "slightly odd", open question | **Resolved:** the protocol is inverted-logic UART, 9600 8N1 (`uart_set_line_inverse` in `Uart.cpp`). Inverted UART idles low — both multimeter readings corroborate Benas's pinout exactly, strengthening the protocol theory |
| Phase 2 uses serial logging over USB1 | The firmware has **no serial-console logging** — all status + raw TX/RX frames (hex) publish to MQTT `fujitsu/<id>/…`; bench work runs off `mosquitto_sub` |
| — | **New risk found:** the firmware already inverts at the ESP pins, so CN1's level shifter must be non-inverting. Loopback cannot detect a double inversion (it cancels); the decisive check is CN1 Tx idle voltage (~0 V good, ~5 V bad). A ready-made contingency build (`esp32-c3-noinvert` env) patches the inversion out if needed |

## Current state

- `fujitsu-ac/` in this repo: pinned PlatformIO project (pioarduino platform,
  Arduino core 3.x, board `esp32-c3-devkitm-1`), FujitsuAC pinned to audited
  commit `0f3c2d5` (v1.3.16), sketch config kept identical to upstream C3
  defaults so the click-triggered network OTA never changes pin behaviour.
- Not yet compiled (cloud session couldn't fetch the ESP32 toolchain —
  egress-restricted; builds normally on the Mac).
- Board still runs Nathan's ESPHome halcyon firmware; needs `erase_flash` +
  fresh upload.

## Next actions (hardware, on the Mac — full detail in README.md)

1. `pio run` — first compile validation, then erase + `pio run -t upload`
   (hold SW2/PRGM; board disconnected from the AC throughout).
2. Configure via the `faircon-<id>` AP at 192.168.1.1 → WiFi + Mosquitto
   (192.168.50.184:1883) credentials.
3. Bench (USB power only): confirm `Init1 Send` / `No response for 200 ms` on
   MQTT → CN1 loopback echoes Init1 on `debug/received` (proves GPIO20/21) →
   multimeter: CN1 Tx idles ~0 V (proves shifter polarity; if ~5 V, flash
   `pio run -e esp32-c3-noinvert -t upload` and re-measure).
4. Live: disconnect USB, plug into the AC. Success = `Init1 Send` →
   `Init2 Send` → `Running` → climate entity in HA. Then two-way control +
   IR-remote state-sync checks.

## Standing safety gates (unchanged from Rev 2)

1. **H2 jumper stays at 12 V** (supply-select only — cleared by Nathan).
   ⚠️ Never move it to 5 V with the AC cable connected (13.65 V onto the 5 V
   rail destroys the board).
2. **Ground-loop rule:** never connect USB1 and the AC simultaneously; AC pins
   are not galvanically isolated. Flash fully disconnected from the unit.

## Open items

- CN1 = GPIO20/21: still an inference (~90%) — now doubly supported by
  upstream choosing the same pins for C3, but confirm via the loopback test
  before live connection.
- Level-shifter polarity: unknown until the CN1 Tx idle-voltage check.
- Protocol theory: unproven until the unit answers Init1. The multimeter
  readings matching inverted-UART expectations is strong new corroboration.

## Key references

- Firmware: `github.com/Benas09/FujitsuAC` (pinned `0f3c2d5`, v1.3.16;
  UTY-TFSXF3 confirmed for Oceania)
- HA community thread: `community.home-assistant.io/t/fujitsuac-integration/914288`
- Service manual (ASTG30/34KMTC): abc-air.com.au
  (`Fujitsu-ASTG30-34KMTC-service-manua.pdf`)
- Board docs from Nathan (in Tom's files): layout/flashing PDF, template
  halcyon YAML, factory .bin, enclosure STLs
