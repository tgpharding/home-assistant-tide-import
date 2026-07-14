# FujitsuAC — new unit, responds but frame format unrecognized (ASTG34KMTC, Oceania)

Report/log for Benas. Unit answers the handshake but the first frame after
`Running` fails checksum and never populates registers.

## Setup

- **Indoor unit:** Fujitsu ASTG34KMTC (wall split, New Zealand / Oceania)
- **Dongle emulated:** UTY-TFSXF3 (Oceania F-series), via the unit's USB-A
  WiFi-dongle port
- **Firmware:** FujitsuAC v1.3.16, protocol setting `UTY-TFSXW1` (only option)
- **Hardware:** custom — ESP32-C3 (nathansags board), 9600 8N1, TX/RX inverted,
  through a level shifter. Hardware fully bench-verified: byte-perfect
  loopback, clean TX idle, LIN transceiver on the shared pins isolated.
  So the RX path is known-good; the frames below are exactly what the AC
  sends.

## What happens

Handshake progresses to `Running`, then stalls on the first subsequent frame.
Deterministic — byte-identical across every reboot.

```
send      00 00 00 00 04 00 00 00 00 FF FB      (Init1, checksum OK)
send      01 00 00 00 04 00 04 00 01 FF F5      (Init2, checksum OK)
status    Running
received  00 01 01 14 00 01 01
error     invalid checksum
error     No response for 200 ms                (then stuck — no retry)
```

## Analysis

- Reaching `Running` means the AC's Init1 and Init2 replies matched the
  expected `00 00 00 00 01 01 FF FD` / `01 00 00 00 01 01 FF FC`, so the core
  handshake is correct for this unit.
- The stalling frame `00 01 01 14 00 01 01` is **7 bytes**. The `TFSXW1`
  buffer treats byte[4] as the payload length; here byte[4]=`0x00`, forcing a
  7-byte frame, and the checksum (0xFFFF − sum) then fails (0x0101 vs 0xFFE9).
- Suspicion: this unit's frame does **not** carry the length at byte[4], so
  the parser is truncating a longer frame. Note `0x14` (20) at byte[3] — a
  plausible length/type field for this variant. The real frame may be ~20+
  bytes and the 7 bytes logged are just the truncated head.

## What we can provide

Custom hardware, so we can run instrumented builds easily. Happy to supply a
**raw, unframed byte dump** (every RX byte with inter-byte timing, bypassing
the frame parser) so you can see the full frame this unit actually sends —
just say the word and we'll capture it. Also happy to try patched builds.
