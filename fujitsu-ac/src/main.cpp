/*
  Fujitsu ASTG34KMTC — WiFi-dongle-port controller firmware
  Target: nathansags ESP32-C3 board (CN1 JST header via onboard level shifter)

  Adapted from FujitsuAC examples/Controller/Controller.ino (Benas09).
  Pin choices below deliberately match the library's own ESP32-C3 defaults,
  so this build behaves identically to the upstream prebuilt fw/esp32c3.bin
  and the dongle's built-in network OTA update (which pulls that binary)
  never changes behaviour.

  Board mapping (nathansags CN1: G / Rx / Tx / V+):
    CN1 Rx -> GPIO20 (ESP RX, AC TX)   \  routed through the onboard level
    CN1 Tx -> GPIO21 (ESP TX, AC RX)   /  shifter; external side ~5V logic
    H2 jumper stays at 12V (V+ 13.65V from the AC feeds the buck converter).

  LEDs / reset button are disabled: the library's classic-ESP32 defaults
  (18/19) are this board's native USB pins. GPIO10 is a known-good LED
  output (Nathan's WiFi LED) if a status LED is ever wanted, but the
  library drives LEDs with inverted PWM (active-low), and the polarity of
  the GPIO10 LED is unverified — left off rather than risk an always-on LED.
*/
#include <FujitsuAC.h>
#include <driver/uart.h>

#define RXD2 20
#define TXD2 21
#define UART_PORT UART_NUM_1

#define LED_W -1
#define LED_R -1
#define RESET_BUTTON -1

FujitsuAC::FujitsuAC fujitsuAC = FujitsuAC::FujitsuAC(
    UART_PORT,
    RXD2,
    TXD2,
    LED_W,
    LED_R,
    RESET_BUTTON
);

void setup() {
    fujitsuAC.setup();

    // GPIO20/21 are also the C3's default UART0 console pins. The library
    // claims them for UART1 in its (global) constructor, but console fixups
    // during setup — e.g. the low-CPU-speed feature's frequency change —
    // steal them back, leaving CN1 Tx parked at the console's idle-high and
    // the AC frames routed nowhere (verified on the bench: CN1 idled at 5V
    // and loopback was dead until the console was moved off these pins).
    // Build flags route the console to USB-CDC; this re-asserts UART1's
    // claim after everything in setup() has run.
    uart_set_pin(UART_PORT, TXD2, RXD2, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
#ifdef FUJITSU_NO_LINE_INVERSE
    uart_set_line_inverse(UART_PORT, UART_SIGNAL_INV_DISABLE);
#else
    uart_set_line_inverse(UART_PORT, UART_SIGNAL_TXD_INV | UART_SIGNAL_RXD_INV);
#endif

    // Deliver received bytes promptly. With this build's driver defaults the
    // RX timeout interrupt never fired: an 11-byte frame sat in the hardware
    // FIFO (bench-verified — a byte-perfect Init1 loopback echo only surfaced
    // when a line break forced a FIFO flush) because small frames never reach
    // the FIFO-full threshold. Fire after 2 idle symbol times or 20 buffered
    // bytes, whichever comes first.
    uart_set_rx_timeout(UART_PORT, 2);
    uart_set_rx_full_threshold(UART_PORT, 20);
}

void loop() {
    fujitsuAC.loop();
}
