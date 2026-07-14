/*
  Raw frame sniffer — captures the AC's true (untruncated) frame format.

  The FujitsuAC TFSXW1 parser decides a frame is complete at byte[4]+7 bytes.
  On the ASTG34KMTC the first post-handshake reply came through as a 7-byte
  frame 00 01 01 14 00 01 01 that fails checksum — very likely a LONGER frame
  the parser truncated because this unit doesn't carry its length at byte[4].

  This build replays the known-good handshake (Init1, Init2), then polls a
  single register, and publishes EVERY received byte to MQTT grouped into
  bursts (a >8 ms gap = new burst = frame boundary). No frame-length logic,
  no checksum gating — you see exactly what the unit sends, full length.

  Output (over WiFi/MQTT, so it works plugged into the AC with no USB):
    fujitsu/sniff/tx   <hex>   what we sent
    fujitsu/sniff/rx   <hex>   one received burst (candidate full frame)
    fujitsu/sniff/info <text>  state notes

  Credentials come from the board's own NVS (namespace fujitsu_ac), same as
  the real firmware. Watch with:
    mosquitto_sub -h 192.168.50.184 -u the-terrace -P hannahrox201 -t 'fujitsu/sniff/#' -v
*/
#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include "driver/uart.h"

#define UART_PORT UART_NUM_1
#define PIN_TX 21
#define PIN_RX 20

// Known-good handshake frames (verified: AC answers both, reaches "Running").
static const uint8_t INIT1[] = {0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFB};
static const uint8_t INIT2[] = {0x01, 0x00, 0x00, 0x00, 0x04, 0x00, 0x04, 0x00, 0x01, 0xFF, 0xF5};
// Minimal registry poll for a single register (0x0001), TFSXW1 format:
//   03 00 00 00 02 <addr_hi> <addr_lo> <cksum_hi> <cksum_lo>
//   cksum = 0xFFFF - 0x03 - 0x02 - 0x00 - 0x01 = 0xFFF9
static const uint8_t POLL[] = {0x03, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01, 0xFF, 0xF9};

static WiFiClient espClient;
static PubSubClient mqtt(espClient);
static String mqttUser, mqttPw;

static char rxLine[400];
static size_t rxLen = 0;
static uint32_t lastRxByteMs = 0;

static void publishHex(const char* topic, const uint8_t* data, size_t n) {
    char line[400];
    size_t p = 0;
    for (size_t i = 0; i < n && p < sizeof(line) - 4; i++) {
        p += snprintf(line + p, sizeof(line) - p, "%02X ", data[i]);
    }
    mqtt.publish(topic, line);
}

static void flushRxBurst() {
    if (rxLen > 0) {
        mqtt.publish("fujitsu/sniff/rx", rxLine);
        rxLen = 0;
        rxLine[0] = '\0';
    }
}

static void pumpRx() {
    uint8_t b;
    while (uart_read_bytes(UART_PORT, &b, 1, 0) == 1) {
        uint32_t now = millis();
        if (rxLen > 0 && (now - lastRxByteMs) > 8) {
            flushRxBurst(); // gap => previous burst was a complete frame
        }
        lastRxByteMs = now;
        if (rxLen < sizeof(rxLine) - 4) {
            rxLen += snprintf(rxLine + rxLen, sizeof(rxLine) - rxLen, "%02X ", b);
        }
    }
}

static void sendFrame(const char* label, const uint8_t* data, size_t n) {
    flushRxBurst();
    mqtt.publish("fujitsu/sniff/info", label);
    publishHex("fujitsu/sniff/tx", data, n);
    uart_write_bytes(UART_PORT, data, n);
}

static void ensureMqtt() {
    while (!mqtt.connected()) {
        mqtt.connect("sniffraw", mqttUser.c_str(), mqttPw.c_str());
        if (!mqtt.connected()) delay(500);
    }
}

void setup() {
    Preferences prefs;
    prefs.begin("fujitsu_ac", true);
    String ssid = prefs.getString("wifi-ssid", "");
    String pw = prefs.getString("wifi-pw", "");
    String mqttIp = prefs.getString("mqtt-ip", "");
    String mqttPort = prefs.getString("mqtt-port", "1883");
    mqttUser = prefs.getString("mqtt-user", "");
    mqttPw = prefs.getString("mqtt-pw", "");
    prefs.end();

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pw.c_str());
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) delay(100);

    mqtt.setServer(mqttIp.c_str(), (uint16_t) mqttPort.toInt());
    mqtt.setBufferSize(512);
    ensureMqtt();

    const uart_config_t cfg = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    uart_driver_install(UART_PORT, 1024, 0, 0, NULL, 0);
    uart_param_config(UART_PORT, &cfg);
    uart_set_pin(UART_PORT, PIN_TX, PIN_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_set_line_inverse(UART_PORT, UART_SIGNAL_TXD_INV | UART_SIGNAL_RXD_INV);

    mqtt.publish("fujitsu/sniff/info", "sniffraw started");

    // Handshake, then let the AC settle, capturing raw the whole time.
    sendFrame("-> Init1", INIT1, sizeof(INIT1));
    for (int i = 0; i < 40; i++) { pumpRx(); delay(10); }   // ~400 ms
    sendFrame("-> Init2", INIT2, sizeof(INIT2));
    for (int i = 0; i < 40; i++) { pumpRx(); delay(10); }
}

void loop() {
    ensureMqtt();
    mqtt.loop();

    // Poll one register every ~1.5 s and capture the raw reply.
    static uint32_t lastPollMs = 0;
    uint32_t now = millis();
    if (now - lastPollMs >= 1500) {
        lastPollMs = now;
        sendFrame("-> Poll reg 0x0001", POLL, sizeof(POLL));
    }

    pumpRx();
    // flush a trailing burst once the line has been quiet a moment
    if (rxLen > 0 && (millis() - lastRxByteMs) > 15) {
        flushRxBurst();
    }
}
