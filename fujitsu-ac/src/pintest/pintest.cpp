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

// every safe input candidate on the C3: 18/19 are native USB (never touch),
// 21 is the driven output, 11-17 are SPI flash. 9 is the boot strap but
// reading it is harmless.
static const int CANDIDATES[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 20};
static const int N = sizeof(CANDIDATES) / sizeof(CANDIDATES[0]);

static bool level = false;
static uint32_t lastToggleMs = 0;
static uint32_t lastReportMs = 0;

static uint32_t samples = 0;
static uint32_t matchSame[N] = {0}, matchOpposite[N] = {0};

void setup() {
    Serial.begin(115200);
    delay(3000); // give USB CDC time to enumerate

    pinMode(PIN_TX, OUTPUT);
    digitalWrite(PIN_TX, LOW);
    for (int i = 0; i < N; i++) {
        pinMode(CANDIDATES[i], INPUT);
    }

    Serial.println();
    Serial.println("pintest v2: GPIO21 square wave 1s HIGH / 1s LOW;");
    Serial.println("watching GPIOs 0-10 and 20 for any pin that follows it.");
    Serial.println("Meter CN1 Tx->G first, then jumper CN1 Rx<->Tx.");
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
        samples++;
        for (int i = 0; i < N; i++) {
            bool v = digitalRead(CANDIDATES[i]);
            if (v == level) matchSame[i]++;
            else matchOpposite[i]++;
        }
    }

    if (now - lastReportMs >= 2000 && samples > 0) {
        lastReportMs = now;

        char hits[128] = "";
        for (int i = 0; i < N; i++) {
            char part[32];
            if (matchSame[i] > samples * 95 / 100) {
                snprintf(part, sizeof(part), " GPIO%d(same)", CANDIDATES[i]);
                strlcat(hits, part, sizeof(hits));
            } else if (matchOpposite[i] > samples * 95 / 100) {
                snprintf(part, sizeof(part), " GPIO%d(inverted)", CANDIDATES[i]);
                strlcat(hits, part, sizeof(hits));
            }
        }

        Serial.printf(
            "GPIO21(TX) now %s | GPIO20 now %s | following GPIO21:%s\n",
            level ? "HIGH" : "LOW",
            digitalRead(20) ? "HIGH" : "LOW",
            hits[0] ? hits : " none"
        );

        samples = 0;
        memset(matchSame, 0, sizeof(matchSame));
        memset(matchOpposite, 0, sizeof(matchOpposite));
    }
}
