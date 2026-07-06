/*
  CN1 pin-mapping / level-shifter diagnostic for the nathansags ESP32-C3 board.

  Drives GPIO21 (expected CN1 Tx) in a slow 1 s square wave a multimeter can
  follow, continuously reads GPIO20 (expected CN1 Rx), and reports both over
  the native-USB serial console (this build enables USB CDC; the AC firmware
  itself has no serial logging).

  Bench procedure (micro-USB only, AC cable disconnected):
    1. pio run -e pintest -t upload      (hold SW2/PRGM while plugging in)
    2. pio device monitor
    3. Meter CN1 Tx -> G: should alternate ~5V/0V in step with the console.
       Same phase as GPIO21 = non-inverting shifter; opposite = inverting;
       stuck at 5V = CN1 Tx is not GPIO21.
    4. Jumper CN1 Rx <-> Tx: the GPIO20 column should start following GPIO21
       (same or opposite phase). "loopback ACTIVE" is printed when the input
       tracks the output for a full cycle.

  GPIO18/19 are the board's native USB pins — never driven here.
*/
#include <Arduino.h>

static const int PIN_TX = 21; // expected CN1 Tx (ESP output toward AC)
static const int PIN_RX = 20; // expected CN1 Rx (ESP input from AC)

static bool level = false;
static uint32_t lastToggleMs = 0;
static uint32_t lastReportMs = 0;

// loopback tracking: does PIN_RX follow PIN_TX (either polarity)?
static uint32_t samples = 0, matchSame = 0, matchOpposite = 0;

void setup() {
    Serial.begin(115200);
    delay(3000); // give USB CDC time to enumerate

    pinMode(PIN_TX, OUTPUT);
    pinMode(PIN_RX, INPUT);
    digitalWrite(PIN_TX, LOW);

    Serial.println();
    Serial.println("pintest: GPIO21 square wave 1s HIGH / 1s LOW; reading GPIO20");
    Serial.println("pintest: meter CN1 Tx->G first, then jumper CN1 Rx<->Tx");
}

void loop() {
    uint32_t now = millis();

    if (now - lastToggleMs >= 1000) {
        lastToggleMs = now;
        level = !level;
        digitalWrite(PIN_TX, level);
    }

    // sample at 50 Hz, skipping the 20 ms after each edge to ignore slew
    static uint32_t lastSampleMs = 0;
    if (now - lastSampleMs >= 20 && now - lastToggleMs >= 20) {
        lastSampleMs = now;
        bool rx = digitalRead(PIN_RX);
        samples++;
        if (rx == level) matchSame++;
        if (rx != level) matchOpposite++;
    }

    if (now - lastReportMs >= 2000 && samples > 0) {
        lastReportMs = now;

        const char* verdict = "loopback INACTIVE (GPIO20 not following)";
        if (matchSame > samples * 95 / 100) {
            verdict = "loopback ACTIVE, same polarity (non-inverting path)";
        } else if (matchOpposite > samples * 95 / 100) {
            verdict = "loopback ACTIVE, OPPOSITE polarity (inverting path)";
        }

        Serial.printf(
            "GPIO21(TX) now %s | GPIO20(RX) now %s | last 2s: %s\n",
            level ? "HIGH" : "LOW",
            digitalRead(PIN_RX) ? "HIGH" : "LOW",
            verdict
        );

        samples = matchSame = matchOpposite = 0;
    }
}
