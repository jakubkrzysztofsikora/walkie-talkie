/**
 * CoreS3 Walkie-Talkie — main entry point
 *
 * Boot sequence (P7 skeleton):
 *   1. Serial init at 115200
 *   2. M5Unified init (BSP: AXP2101 PMU, display, touch, codec rails)
 *   3. Print board info + battery level
 *   4. Loop: heartbeat log every 10 s
 *
 * Future phases wire in: Wi-Fi (P8), Opus codec (P9), I2S audio (P10),
 * WebSocket client (P8), touch PTT (P13), state machine (P8).
 *
 * See: thoughts/shared/research/2026-06-01-cores3-port-spec.md §7
 *      thoughts/shared/plans/2026-06-02-cores3-port-plan.md §P7
 */

#include <Arduino.h>
#include <M5Unified.h>

// ---------------------------------------------------------------------------
// Boot
// ---------------------------------------------------------------------------

void setup() {
    Serial.begin(115200);
    delay(500);  // let USB-CDC enumerate

    Serial.println();
    Serial.println("cores3 walkie-talkie boot");
    Serial.print("M5Unified init... ");

    auto cfg = M5.config();
    cfg.serial_baudrate = 115200;
    M5.begin(cfg);

    Serial.println("OK");
    Serial.print("Board: ");
    Serial.print("Board type: ");
    Serial.println(M5.getBoard());
    Serial.print("Battery: ");
    Serial.print(M5.Power.getBatteryLevel());
    Serial.println("%");

    Serial.println("setup complete — entering idle loop");
}

// ---------------------------------------------------------------------------
// Idle loop (P7 skeleton — replaced by state machine in P8)
// ---------------------------------------------------------------------------

void loop() {
    static unsigned long last_beat = 0;
    unsigned long now = millis();

    if (now - last_beat >= 10000) {
        last_beat = now;
        Serial.print("[idle] uptime=");
        Serial.print(now / 1000);
        Serial.print("s heap_free=");
        Serial.print(ESP.getFreeHeap());
        Serial.print(" psram_free=");
        Serial.println(ESP.getFreePsram());
    }

    M5.update();
    delay(10);
}
