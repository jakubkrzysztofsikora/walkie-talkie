/**
 * CoreS3 Walkie-Talkie — main entry point
 *
 * P7: M5Unified + Wi-Fi
 * P8: WebSocket client + control protocol + state machine
 * P9: Opus codec (micro-opus)
 * P10: Audio I2S via M5Unified Mic/Speaker
 *
 * Spec: thoughts/shared/research/2026-06-01-cores3-port-spec.md
 */

#include <Arduino.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>

#include "config/secrets.h"
#include "audio/opus_stub.h"

// Override default 8KB loopTask stack (CONFIG_ARDUINO_LOOP_STACK_SIZE).
// WiFi + WebSocket + JSON + M5Unified collectively exceed 8KB.
// Declared __attribute__((weak)) in Arduino.h with C++ linkage.
size_t getArduinoLoopTaskStackSize(void) {
    return 16384;  // 16 KB
}

// ---------------------------------------------------------------------------
// State machine
// ---------------------------------------------------------------------------

enum class DeviceState { BOOT, WIFI_CONNECT, WSS_CONNECT, IDLE, SESSION_ACTIVE };
static DeviceState state = DeviceState::BOOT;

// ---------------------------------------------------------------------------
// WebSocket
// ---------------------------------------------------------------------------

static WebSocketsClient ws;

static void send_json(const char* type) {
    StaticJsonDocument<128> doc;
    doc["type"] = type;
    doc["ts"] = millis() / 1000;
    String out;
    serializeJson(doc, out);
    ws.sendTXT(out);
}

static void send_keepalive() {
    StaticJsonDocument<64> doc;
    doc["type"] = "keepalive";
    doc["ts"] = millis() / 1000;
    String out;
    serializeJson(doc, out);
    ws.sendTXT(out);
}

static void ws_handler(WStype_t type, uint8_t* payload, size_t len) {
    switch (type) {
        case WStype_DISCONNECTED:
            if (state == DeviceState::SESSION_ACTIVE) state = DeviceState::WSS_CONNECT;
            break;
        case WStype_CONNECTED:
            Serial.println("[ws] connected");
            break;
        case WStype_TEXT: {
            StaticJsonDocument<256> doc;
            if (deserializeJson(doc, payload, len)) return;
            const char* t = doc["type"] | "";
            Serial.printf("[ws] <- %s\n", t);
            if (!strcmp(t, "hello") && state == DeviceState::WSS_CONNECT)
                state = DeviceState::IDLE;
            else if (!strcmp(t, "session_started"))
                state = DeviceState::SESSION_ACTIVE;
            else if (!strcmp(t, "session_ended"))
                state = DeviceState::IDLE;
            break;
        }
        case WStype_BIN:
            // TTS Opus audio from server → decode → speaker
            if (state == DeviceState::SESSION_ACTIVE) {
                int16_t pcm[OPUS_FRAME_SAMPLES];
                int samples = opus_decode_frame(payload, len, pcm);
                if (samples > 0) {
                    M5.Speaker.playRaw(pcm, samples, OPUS_SAMPLE_RATE, false);
                }
            }
            break;
        default: break;
    }
}

// ---------------------------------------------------------------------------
// Wi-Fi
// ---------------------------------------------------------------------------

static bool wifi_connect(const char* ssid, const char* pass, unsigned long timeout_ms = 30000) {
    Serial.printf("Wi-Fi: %s... ", ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > timeout_ms) { Serial.println("FAIL"); return false; }
        delay(500); Serial.print(".");
    }
    Serial.printf("OK  IP=%s RSSI=%ddBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    return true;
}

// ---------------------------------------------------------------------------
// Boot
// ---------------------------------------------------------------------------

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\ncores3 walkie-talkie boot");

    // M5Unified BSP
    // Audio (mic+speaker) deferred to P10 — M5.Mic/Speaker I2S callbacks
    // overflow the default 8KB loopTask stack when combined with WS+WiFi.
    auto cfg = M5.config();
    cfg.serial_baudrate = 115200;
    M5.begin(cfg);
    Serial.printf("Board:%d Battery:%d%%\n", M5.getBoard(), M5.Power.getBatteryLevel());

    // Opus codec
    if (!opus_stub_init()) {
        Serial.println("[opus] FAILED — audio disabled");
    }

    // Wi-Fi
    if (!wifi_connect(WIFI_SSID, WIFI_PASS)) {
        Serial.println("Wi-Fi FAILED — retrying in loop");
    } else {
        state = DeviceState::WIFI_CONNECT;
    }

    // WebSocket
    ws.begin(BACKEND_HOST, BACKEND_PORT, "/ws/cores3?device_token=" DEVICE_TOKEN);
    ws.onEvent(ws_handler);
    ws.setReconnectInterval(5000);

    Serial.println("setup complete");
}

// ---------------------------------------------------------------------------
// Loop
// ---------------------------------------------------------------------------

void loop() {
    static unsigned long last_beat = 0, last_keepalive = 0;
    static bool demo_session_sent = false;
    unsigned long now = millis();

    if (!WiFi.isConnected()) wifi_connect(WIFI_SSID, WIFI_PASS);
    ws.loop();

    // State transitions
    if (state == DeviceState::WIFI_CONNECT && WiFi.isConnected())
        { state = DeviceState::WSS_CONNECT; Serial.println("state→WSS_CONNECT"); }

    // Keepalive
    if ((state == DeviceState::IDLE || state == DeviceState::WSS_CONNECT) && now - last_keepalive >= 30000) {
        last_keepalive = now; send_keepalive();
    }

    // Audio loop (P10): mic → Opus encode → WS send
    // Deferred — requires FreeRTOS task pinning to avoid loopTask stack overflow.
    // Current wiring compiles and links but M5.Mic.record + opus_encode + ws.sendBIN
    // exceeds the default 8KB loopTask stack. See platformio.ini STACK_SIZE note.

    // Demo: auto-start session after 8s, end after 25s (P13 replaces this with touch PTT)
    if (state == DeviceState::IDLE && !demo_session_sent && now > 8000) {
        demo_session_sent = true;
        send_json("session_start");
        Serial.println("state→SESSION_ACTIVE (demo)");
    }
    if (state == DeviceState::SESSION_ACTIVE && demo_session_sent && now > 25000) {
        send_json("session_end");
        state = DeviceState::IDLE;
        demo_session_sent = false;
        Serial.println("state→IDLE (demo end)");
    }

    // Heartbeat
    if (now - last_beat >= 10000) {
        last_beat = now;
        Serial.printf("[idle] uptime=%ds wifi=%s rssi=%ddBm heap=%d psram=%d state=%d\n",
                      now / 1000,
                      WiFi.isConnected() ? WiFi.localIP().toString().c_str() : "DOWN",
                      WiFi.RSSI(), ESP.getFreeHeap(), ESP.getFreePsram(), (int)state);
    }

    M5.update();
}
