/*
  Staged UART RX delivery hunt.

  Known: the bare UART echo test delivers received bytes instantly, while the
  full FujitsuAC firmware leaves them stranded in the RX FIFO. This build
  starts from the bare test and switches on one firmware subsystem per stage,
  counting echoed bytes (the LIN transceiver's echo is the signal source —
  no jumper needed). The stage where the count collapses names the culprit.

  Stages (15 s each):
    0 baseline (nothing on)
    1 setCpuFrequencyMhz(80)      - mimics low_cpu_speed
    2 setCpuFrequencyMhz(160)     - and back
    3 WiFi.begin                  - credentials read from the board's own NVS
    4 WiFi.setSleep(true)         - mimics wifi_sleep
    5 configTime (NTP)            - mimics NetworkUpdater clock sync
    6 MDNS + ArduinoOTA.begin
    7 MQTT connect + 1 Hz publish - mimics the bridge
    8 summary, then idle

  WiFi/MQTT credentials come from Preferences namespace "fujitsu_ac" (written
  by the real firmware's captive portal) — nothing is hardcoded here. If a
  stage's prerequisite fails (e.g. WiFi down), it says so and carries on.

  Bench use: NO jumper on CN1, board on USB. Flash, `pio device monitor`,
  wait ~2.5 minutes, paste the whole output.
*/
#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include "driver/uart.h"

#define UART_PORT UART_NUM_1
#define PIN_TX 21
#define PIN_RX 20

static const uint8_t FRAME[] = {
    0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFB
};

static String wifiSsid, wifiPw, mqttIp, mqttPort, mqttUser, mqttPw;

static WiFiClient espClient;
static PubSubClient mqtt(espClient);

static int stage = 0;
static uint32_t stageStartMs = 0;
static uint32_t stageRxBytes = 0;
static bool otaStarted = false;
static bool mqttStage = false;
static bool done = false;

static const char* STAGE_NAMES[] = {
    "0 baseline",
    "1 cpu 80MHz",
    "2 cpu 160MHz",
    "3 wifi connect",
    "4 wifi sleep on",
    "5 ntp configTime",
    "6 mdns + ota",
    "7 mqtt publish",
};
static const int N_STAGES = sizeof(STAGE_NAMES) / sizeof(STAGE_NAMES[0]);

static void enterStage(int s) {
    switch (s) {
        case 1:
            setCpuFrequencyMhz(80);
            break;
        case 2:
            setCpuFrequencyMhz(160);
            break;
        case 3: {
            WiFi.mode(WIFI_STA);
            WiFi.begin(wifiSsid.c_str(), wifiPw.c_str());
            uint32_t t0 = millis();
            while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000) {
                delay(100);
            }
            Serial.printf("  (wifi: %s)\n",
                          WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "FAILED");
            break;
        }
        case 4:
            WiFi.setSleep(true);
            break;
        case 5:
            configTime(0, 0, "pool.ntp.org");
            break;
        case 6:
            MDNS.begin("stagetest");
            ArduinoOTA.begin();
            otaStarted = true;
            break;
        case 7: {
            mqtt.setServer(mqttIp.c_str(), (uint16_t) mqttPort.toInt());
            bool ok = mqtt.connect("stagetest", mqttUser.c_str(), mqttPw.c_str());
            Serial.printf("  (mqtt connect: %s)\n", ok ? "OK" : "FAILED");
            mqttStage = true;
            break;
        }
        default:
            break;
    }
}

void setup() {
    Serial.begin(115200);
    delay(3000);

    Preferences prefs;
    prefs.begin("fujitsu_ac", true);
    wifiSsid = prefs.getString("wifi-ssid", "");
    wifiPw = prefs.getString("wifi-pw", "");
    mqttIp = prefs.getString("mqtt-ip", "");
    mqttPort = prefs.getString("mqtt-port", "1883");
    mqttUser = prefs.getString("mqtt-user", "");
    mqttPw = prefs.getString("mqtt-pw", "");
    prefs.end();

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
        "stagetest init: install=%d config=%d pins=%d inv=%d tout=%d thresh=%d (0=OK)\n",
        eInstall, eConfig, ePins, eInv, eTout, eThresh
    );
    Serial.printf("stagetest: ssid='%s' mqtt=%s:%s user='%s' (from NVS)\n",
                  wifiSsid.c_str(), mqttIp.c_str(), mqttPort.c_str(), mqttUser.c_str());
    Serial.println("stagetest: 8 stages x 15s; healthy stage ~ 165 echo bytes. NO jumper on CN1.");

    stageStartMs = millis();
    enterStage(0);
}

void loop() {
    uint32_t now = millis();

    static uint32_t lastSendMs = 0;
    if (now - lastSendMs >= 1000) {
        lastSendMs = now;
        uart_write_bytes(UART_PORT, FRAME, sizeof(FRAME));
        if (mqttStage && mqtt.connected()) {
            mqtt.publish("fujitsu/stagetest", "tick");
        }
    }

    if (otaStarted) {
        ArduinoOTA.handle();
    }
    if (mqttStage) {
        mqtt.loop();
    }

    uint8_t buf[64];
    int n = uart_read_bytes(UART_PORT, buf, sizeof(buf), 0);
    if (n > 0) {
        stageRxBytes += n;
    }

    if (done || now - stageStartMs < 15000) {
        return;
    }

    Serial.printf("%-16s: %u echo bytes%s\n",
                  STAGE_NAMES[stage], (unsigned) stageRxBytes,
                  stageRxBytes == 0 ? "  <-- ECHO DIED HERE" : "");

    stage++;
    stageRxBytes = 0;
    stageStartMs = now;

    if (stage >= N_STAGES) {
        done = true;
        Serial.println("stagetest: complete. A stage with ~0 bytes after healthy earlier");
        Serial.println("stagetest: stages identifies the subsystem that kills RX delivery.");
        return;
    }

    enterStage(stage);
}
