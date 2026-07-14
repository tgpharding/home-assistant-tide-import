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

## Current state (Rev 3.1, post-bench — July 2026)

Bench verification ran across multiple sessions and settled every hardware
question. Proven working: CN1 = RX GPIO20 / TX GPIO21 (confirmed), level
shifter non-inverting both directions at 5 V logic, TX drive verified at the
connector, RX verified chip-side, and byte-perfect 9600-baud inverted-UART
reception on a bare test build. Firmware fixes landed along the way: USB-CDC
console flags (the C3's UART0 console otherwise claims GPIO20/21), runtime
UART driver reinstall with explicit RX delivery thresholds, platform pinned
to pioarduino 51.03.07.

**Two blockers found, both must be resolved before the AC test:**

1. **U6 (LIN transceiver) shares GPIO20/21 with CN1 and is always active.**
   It echoes all UART TX back into GPIO20 (LIN behaviour; its dominant-timeout
   is why slow DC tests missed it) and its RXD output fights the CN1 receive
   path — an external device driving CN1 Rx gets corrupted to junk. No spare
   GPIO disables it (full sweep, `uarttest` env). Hardware fix required:
   Nathan has been asked for the cleanest disconnect (likely a series R or a
   pin lift on the 8-pin part by the LIN terminal, silk U55). Nathan's
   "CN1 and LIN can only be used one-or-the-other until v3" was this,
   literally.
2. **The full FujitsuAC firmware stalls UART RX delivery** — received frames
   sit in the hardware FIFO (only surfaced by a break-forced flush), while
   the bare test build delivers instantly with identical UART config. Suspect
   subsystem unknown; the `stagetest` env switches firmware subsystems on one
   stage at a time (cpu clock / wifi / sleep / ntp / mdns+ota / mqtt) to name
   it. Runnable on the bench now — the U6 echo is the test signal.

Diagnostic envs in this project: `pintest` (GPIO square-wave pin mapping),
`uarttest` (bare UART echo + GPIO sweep for U6 enable), `stagetest` (RX
delivery hunt). Device MQTT id: `c8b4e7b2f180`, name StudyAC.

## Next actions

1. Tom → Nathan: U6 disconnect question (drafted in session). Optionally:
   macro photo of U55 marking + back-of-board photo for independent part ID.
2. Bench: run `stagetest` (~2.5 min), read which stage kills RX delivery;
   firmware fix follows from that.
3. After BOTH fixes: reflash `esp32-c3-devkitm-1` env, jumper-at-boot echo
   test must show self-propelled `debug/received` + `Terminated Init1`, then
   live AC test (`Init1 Send` → `Init2 Send` → `Running` → HA climate
   entity).
4. Decision point if U6 fix is unattractive: Benas's ready-made dongle
   (proven on Oceania UTY-TFSXF3 units) and Nathan's board returns to spares.
   All firmware/protocol work transfers.

## Standing safety gates (unchanged from Rev 2)

1. **H2 jumper stays at 12 V** (supply-select only — cleared by Nathan).
   ⚠️ Never move it to 5 V with the AC cable connected (13.65 V onto the 5 V
   rail destroys the board).
2. **Ground-loop rule:** never connect USB1 and the AC simultaneously; AC pins
   are not galvanically isolated. Flash fully disconnected from the unit.

## Live-test result (July 2026) — PROTOCOL CONFIRMED, unit needs decode support

Plugged into the AC. **The ASTG34KMTC responded** — the first data ever
received from this port. Handshake ran `Init1 → Init2 → Running`; the AC's
Init replies matched the expected frames, so the link is fully bidirectional
and the protocol theory (unproven since the first multimeter reading) is
**confirmed**.

Remaining blocker is now purely firmware: the first frame after `Running`,
`00 01 01 14 00 01 01`, fails the TFSXW1 checksum (deterministic across every
reboot) and the controller stalls without retrying. No config fix exists —
`UTY-TFSXW1` is the only implemented protocol (`TFSXJ4` is a commented-out
stub). This unit's frame format isn't recognized by the one controller.

**This is a FujitsuAC protocol-support task for Benas**, not a hardware or
integration problem — and it is hardware-agnostic (Benas's own ready-made
dongle runs the same firmware and would hit the same wall on this unit, so
buying one is no longer a fallback). See `BENAS-REPORT.md`. All hardware and
firmware-plumbing work is done and verified.

## Bench/hardware status: COMPLETE

Everything below is proven working: CN1 = RX GPIO20 / TX GPIO21, non-inverting
level shifter, TX + RX end-to-end at 9600 8N1 inverted, byte-perfect loopback.
Firmware fixes shipped: USB-CDC console (frees GPIO20/21 from the C3 console),
runtime UART driver reinstall + RX thresholds, platform pinned to pioarduino
51.03.07. **R54/R55 removed** on the board to isolate the always-on LIN
transceiver (U55/U6) that shares the UART pins — this was the single fix that
made external RX work (Nathan confirmed R54/R55; it was the "CN1 or LIN, not
both" limitation, literally).

## Open items

- **Benas:** decode support for the ASTG34KMTC frame format (send
  `BENAS-REPORT.md` + log to the community thread / email). Offer the raw
  unframed byte dump if he wants the full frame.
- If/when Benas ships support: network-update the dongle (or reflash) and
  resume from the live test — climate entity, two-way control, IR sync.

## Key references

- Firmware: `github.com/Benas09/FujitsuAC` (pinned `0f3c2d5`, v1.3.16;
  UTY-TFSXF3 confirmed for Oceania)
- HA community thread: `community.home-assistant.io/t/fujitsuac-integration/914288`
- Service manual (ASTG30/34KMTC): abc-air.com.au
  (`Fujitsu-ASTG30-34KMTC-service-manua.pdf`)
- Board docs from Nathan (in Tom's files): layout/flashing PDF, template
  halcyon YAML, factory .bin, enclosure STLs
