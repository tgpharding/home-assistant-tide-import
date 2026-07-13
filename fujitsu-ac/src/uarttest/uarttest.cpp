/*
  Bare-metal UART RX delivery diagnostic — no FujitsuAC library, no WiFi.

  Configures UART1 exactly like the AC firmware (9600 8N1, inverted TX/RX,
  CN1 pins GPIO20/21, explicit RX timeout + threshold), transmits the Init1
  frame once per second, and reports over the native-USB console:
    fifo  = bytes sitting in the hardware RX FIFO (undelivered)
    ring  = bytes delivered to the software ring buffer, not yet read
    intst = raw UART interrupt STATUS register
    ena   = UART interrupt ENABLE register
  plus a "received:" hex dump of anything that reaches software.

  Bench use: flash, fit the CN1 Rx<->Tx jumper, watch `pio device monitor`.
  Healthy: "received: 00 00 00 00 04 00 00 00 00 FF FB" once per second and
  fifo staying 0. Broken: fifo climbing by 11 per second with no received
  lines — and intst/ena then show exactly which events are asserting vs
  enabled.
*/
#include <Arduino.h>
#include "driver/uart.h"
#include "hal/uart_ll.h"
#include "soc/gpio_reg.h"
#include "soc/gpio_sig_map.h"

#define UART_PORT UART_NUM_1
#define PIN_TX 21
#define PIN_RX 20

static const uint8_t FRAME[] = {
    0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFB
};

void setup() {
    Serial.begin(115200);
    delay(3000); // let USB CDC enumerate

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
        "uarttest init: install=%d config=%d pins=%d inv=%d tout=%d thresh=%d (0=OK)\n",
        eInstall, eConfig, ePins, eInv, eTout, eThresh
    );

    Serial.println("sending Init1 frame every 1s; jumper CN1 Rx<->Tx");
}

void loop() {
    static uint32_t lastSendMs = 0, lastDiagMs = 0, lastMatrixMs = 0;
    uint32_t now = millis();

    // GPIO matrix forensics, reprinted so a late-attaching monitor sees it:
    // who drives each pad, and where the UART receivers read from.
    // out_sel: 128 (0x80) = plain GPIO, 9 = U1TXD, 6 = U0TXD. oe = pad output
    // enabled. in_sel bit7 (0x80) set = routed via matrix from pad in low bits.
    if (now - lastMatrixMs >= 10000) {
        lastMatrixMs = now;
        uint32_t enable = REG_READ(GPIO_ENABLE_REG);
        Serial.printf(
            "matrix: pad20 out_sel=%u oe=%u | pad21 out_sel=%u oe=%u | U1RXD in_sel=0x%02x | U0RXD in_sel=0x%02x\n",
            (unsigned) (REG_READ(GPIO_FUNC0_OUT_SEL_CFG_REG + 20 * 4) & 0xFF),
            (unsigned) ((enable >> 20) & 1),
            (unsigned) (REG_READ(GPIO_FUNC0_OUT_SEL_CFG_REG + 21 * 4) & 0xFF),
            (unsigned) ((enable >> 21) & 1),
            (unsigned) (REG_READ(GPIO_FUNC0_IN_SEL_CFG_REG + U1RXD_IN_IDX * 4) & 0xFF),
            (unsigned) (REG_READ(GPIO_FUNC0_IN_SEL_CFG_REG + U0RXD_IN_IDX * 4) & 0xFF)
        );
    }

    if (now - lastSendMs >= 1000) {
        lastSendMs = now;
        uart_write_bytes(UART_PORT, FRAME, sizeof(FRAME));
    }

    if (now - lastDiagMs >= 1000) {
        lastDiagMs = now;
        uart_dev_t *hw = UART_LL_GET_HW(UART_PORT);
        size_t ring = 0;
        uart_get_buffered_data_len(UART_PORT, &ring);
        Serial.printf(
            "fifo=%u ring=%u intst=0x%08x ena=0x%08x\n",
            (unsigned) uart_ll_get_rxfifo_len(hw),
            (unsigned) ring,
            (unsigned) uart_ll_get_intsts_mask(hw),
            (unsigned) uart_ll_get_intr_ena_status(hw)
        );
    }

    uint8_t buf[64];
    int n = uart_read_bytes(UART_PORT, buf, sizeof(buf), 0);
    if (n > 0) {
        Serial.print("received:");
        for (int i = 0; i < n; i++) {
            Serial.printf(" %02X", buf[i]);
        }
        Serial.println();
    }
}
