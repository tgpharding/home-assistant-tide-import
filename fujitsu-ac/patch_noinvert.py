# PlatformIO pre-build script for the esp32-c3-noinvert environment.
#
# The FujitsuAC library configures the AC-facing UART as inverted logic
# (idle-low), which is correct when the wiring to the AC is non-inverting.
# If the nathansags board's CN1 level shifter turns out to invert the signal
# (bench check: CN1 Tx idles ~5V instead of ~0V under the stock build), the
# shifter and the firmware double-invert and the AC sees the wrong polarity.
# This script patches the env-local copy of the library so the ESP outputs
# normal (idle-high) UART and the shifter supplies the inversion.
#
# It edits only .pio/libdeps/<env>/... — the pinned upstream source is never
# touched — and fails the build loudly if the expected line isn't found
# (e.g. after bumping the library pin), rather than silently building an
# unpatched binary.
import pathlib

Import("env")

uart_cpp = pathlib.Path(
    env.subst("$PROJECT_LIBDEPS_DIR"), env.subst("$PIOENV"), "FujitsuAC", "src", "Uart.cpp"
)

INVERTED = "uart_set_line_inverse(_uart_port, UART_SIGNAL_TXD_INV | UART_SIGNAL_RXD_INV);"
PATCHED = "uart_set_line_inverse(_uart_port, UART_SIGNAL_INV_DISABLE);"

source = uart_cpp.read_text()

if PATCHED in source:
    print("patch_noinvert: already patched, nothing to do")
elif INVERTED in source:
    uart_cpp.write_text(source.replace(INVERTED, PATCHED))
    print(f"patch_noinvert: UART inversion disabled in {uart_cpp}")
else:
    raise SystemExit(
        f"patch_noinvert: expected uart_set_line_inverse call not found in {uart_cpp}; "
        "library changed upstream — re-audit before building this environment"
    )
