/*
  U6 (LIN transceiver) enable-pin hunt.

  Established on this board: the LIN transceiver shares GPIO20/21 with CN1
  and echoes all UART TX back onto GPIO20 (LIN transceivers do this by
  design; its dominant-timeout is also why slow DC tests saw nothing). The
  GPIO matrix registers proved the chip itself does not drive pad 20.

  This build transmits the Init1 frame once per second (U6 obligingly echoes
  it), then sweeps every spare GPIO — driving each LOW for 4 s, then HIGH
  for 4 s — while counting echoed bytes per stage. If a stage's count drops
  to zero, that GPIO+level put U6 to sleep: it's the enable/sleep pin, and
  the AC firmware can hold it there forever.

  Bench use: NO jumper on CN1. Flash, `pio device monitor`, let the sweep
  finish (~2 min), paste the whole output.

  Sweep pins: 0-10 (spare/LED/straps — safe to drive at runtime).
  Excluded: 18/19 (native USB), 20/21 (the UART itself), 11-17 (SPI flash).
*/
#include <Arduino.h>
#include "driver/uart.h"

#define UART_PORT UART_NUM_1
#define PIN_TX 21
#define PIN_RX 20

static const uint8_t FRAME[] = {
    0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFB
};

static const int SWEEP[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
static const int N_SWEEP = sizeof(SWEEP) / sizeof(SWEEP[0]);

// stage -1 = baseline; then 2 stages per pin (LOW, HIGH); then done
static int stage = -1;
static uint32_t stageStartMs = 0;
static uint32_t stageRxBytes = 0;
static uint32_t baselineBytes = 0;
static bool done = false;

static void printStageResult() {
    if (stage == -1) {
        baselineBytes = stageRxBytes;
        Serial.printf("baseline (nothing driven): %u echo bytes\n", (unsigned) stageRxBytes);
        return;
    }

    int pin = SWEEP[stage / 2];
    const char* level = (stage % 2 == 0) ? "LOW " : "HIGH";
    const char* verdict = "";
    if (baselineBytes > 0 && stageRxBytes == 0) {
        verdict = "  <-- ECHO STOPPED: candidate U6 enable pin!";
    }
    Serial.printf("GPIO%-2d=%s: %u echo bytes%s\n", pin, level, (unsigned) stageRxBytes, verdict);
}

void setup() {
    Serial.begin(115200);
    delay(3000);

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
    esp_err_t ePins = uart_set_pin(UART_PORT, PIN_TX, PIN_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    esp_err_t eInv = uart_set_line_inverse(UART_PORT, UART_SIGNAL_TXD_INV | UART_SIGNAL_RXD_INV);
    esp_err_t eTout = uart_set_rx_timeout(UART_PORT, 2);
    esp_err_t eThresh = uart_set_rx_full_threshold(UART_PORT, 20);

    Serial.println();
    Serial.printf(
        "u6hunt init: install=%d config=%d pins=%d inv=%d tout=%d thresh=%d (0=OK)\n",
        eInstall, eConfig, ePins, eInv, eTout, eThresh
    );
    Serial.println("u6hunt: 12s baseline, then each GPIO 0-10 driven LOW 4s / HIGH 4s.");
    Serial.println("u6hunt: looking for the stage where echo bytes drop to zero. NO jumper on CN1.");

    stageStartMs = millis();
}

void loop() {
    uint32_t now = millis();

    // transmit once per second; U6 echoes it back
    static uint32_t lastSendMs = 0;
    if (now - lastSendMs >= 1000) {
        lastSendMs = now;
        uart_write_bytes(UART_PORT, FRAME, sizeof(FRAME));
    }

    // count everything echoed back
    uint8_t buf[64];
    int n = uart_read_bytes(UART_PORT, buf, sizeof(buf), 0);
    if (n > 0) {
        stageRxBytes += n;
    }

    if (done) {
        return;
    }

    uint32_t stageLen = (stage == -1) ? 12000 : 4000;
    if (now - stageStartMs < stageLen) {
        return;
    }

    // stage finished: report, tidy up, advance
    printStageResult();

    if (stage >= 0 && stage % 2 == 1) {
        pinMode(SWEEP[stage / 2], INPUT); // release previous pin after its HIGH stage
    }

    stage++;
    stageRxBytes = 0;
    stageStartMs = now;

    if (stage >= N_SWEEP * 2) {
        done = true;
        Serial.println("u6hunt: sweep complete. If no candidate was flagged, U6 has no");
        Serial.println("u6hunt: GPIO-controlled enable and needs a hardware approach.");
        return;
    }

    int pin = SWEEP[stage / 2];
    int level = (stage % 2 == 0) ? LOW : HIGH;
    pinMode(pin, OUTPUT);
    digitalWrite(pin, level);
}
