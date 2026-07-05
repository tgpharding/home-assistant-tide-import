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
}

void loop() {
    fujitsuAC.loop();
}
