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

// The library installs its UART driver in a global constructor, before
// Arduino/FreeRTOS init completes. Two bench-verified consequences on the C3:
// the UART0 console reclaims GPIO20/21 (its default pins) during setup, and
// received frames sit undelivered in the RX FIFO (a byte-perfect loopback
// echo only surfaced when a line break forced a FIFO flush — the RX timeout
// interrupt never fired). Reinstall the driver cleanly at the end of setup:
// proper runtime context, explicit pins, inversion, and delivery thresholds
// (2 idle symbol times or 20 buffered bytes, whichever comes first). The
// library keeps working through the reinstalled driver — it only holds the
// port number.
static void reinitUart() {
    uart_driver_delete(UART_PORT);

    const uart_config_t cfg = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };

    esp_err_t eInstall = uart_driver_install(UART_PORT, 1024, 0, 0, NULL, 0);
    esp_err_t eConfig = uart_param_config(UART_PORT, &cfg);
    esp_err_t ePins = uart_set_pin(UART_PORT, TXD2, RXD2, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
#ifdef FUJITSU_NO_LINE_INVERSE
    esp_err_t eInv = uart_set_line_inverse(UART_PORT, UART_SIGNAL_INV_DISABLE);
#else
    esp_err_t eInv = uart_set_line_inverse(UART_PORT, UART_SIGNAL_TXD_INV | UART_SIGNAL_RXD_INV);
#endif
    esp_err_t eTout = uart_set_rx_timeout(UART_PORT, 2);
    esp_err_t eThresh = uart_set_rx_full_threshold(UART_PORT, 20);

    Serial.printf(
        "uart reinit: install=%d config=%d pins=%d inv=%d tout=%d thresh=%d (0=OK)\n",
        eInstall, eConfig, ePins, eInv, eTout, eThresh
    );
}

void setup() {
    Serial.begin(115200); // native-USB console, diagnostics only
    fujitsuAC.setup();
    reinitUart();
}

void loop() {
    fujitsuAC.loop();

    // bench diagnostic: how many received bytes the driver has delivered and
    // the library hasn't consumed yet (should hover at 0 when healthy; bytes
    // appearing here at all proves interrupt-driven RX delivery works)
    static uint32_t lastDiagMs = 0;
    if (millis() - lastDiagMs >= 2000) {
        lastDiagMs = millis();
        size_t buffered = 0;
        uart_get_buffered_data_len(UART_PORT, &buffered);
        Serial.printf("uart rx buffered: %u\n", (unsigned) buffered);
    }
}
