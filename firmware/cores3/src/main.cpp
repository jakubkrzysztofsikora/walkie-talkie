/**
 * CoreS3 Walkie-Talkie — main entry point
 *
 * Boot sequence:
 *   1. Serial + M5Unified BSP init
 *   2. Wi-Fi connect
 *   3. WebSocket connect to backend /ws/cores3
 *   4. State machine: IDLE ↔ SESSION_ACTIVE
 *   5. Future: Opus audio (P9), I2S (P10), touch PTT (P13)
 */

#include <Arduino.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <WebSocketsClient.h>

// JSON helpers (ArduinoJson)
#include <ArduinoJson.h>

// Secrets: Wi-Fi SSID/PASS, backend host/port, device token
#include "config/secrets.h"

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

#define SERIAL_BAUD      115200
#define WS_RECONNECT_MS  5000    // retry WebSocket every 5s
#define KEEPALIVE_MS     30000   // send keepalive every 30s (spec §5)
#define HEARTBEAT_MS     10000   // serial heartbeat

// ---------------------------------------------------------------------------
// State machine (spec §7)
// ---------------------------------------------------------------------------

enum class DeviceState {
    BOOT,
    WIFI_CONNECT,
    WSS_CONNECT,
    IDLE,
    SESSION_ACTIVE,
};

static DeviceState state = DeviceState::BOOT;

// ---------------------------------------------------------------------------
// WebSocket client
// ---------------------------------------------------------------------------

static WebSocketsClient ws;

static void ws_event_handler(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_DISCONNECTED:
            Serial.println("[ws] disconnected");
            if (state == DeviceState::SESSION_ACTIVE) {
                state = DeviceState::WSS_CONNECT;
            }
            break;

        case WStype_CONNECTED:
            Serial.println("[ws] connected");
            break;

        case WStype_TEXT: {
            // Parse JSON control messages from server
            StaticJsonDocument<512> doc;
            DeserializationError err = deserializeJson(doc, payload, length);
            if (err) {
                Serial.printf("[ws] bad JSON: %s\n", err.c_str());
                return;
            }
            const char* msg_type = doc["type"] | "";
            Serial.printf("[ws] ← %s\n", msg_type);

            if (strcmp(msg_type, "hello") == 0) {
                const char* character = doc["current_character"] | "radek";
                Serial.printf("[ws] hello — character=%s\n", character);
                if (state == DeviceState::WSS_CONNECT) {
                    state = DeviceState::IDLE;
                }
            } else if (strcmp(msg_type, "session_started") == 0) {
                Serial.printf("[ws] session_started — conv=%s\n", doc["conversation_id"] | "?");
                state = DeviceState::SESSION_ACTIVE;
            } else if (strcmp(msg_type, "session_ended") == 0) {
                Serial.println("[ws] session_ended");
                state = DeviceState::IDLE;
            } else if (strcmp(msg_type, "error") == 0) {
                Serial.printf("[ws] error: %s\n", doc["detail"] | "?");
            } else if (strcmp(msg_type, "tool_event") == 0) {
                Serial.printf("[ws] tool_event: %s\n", doc["tool"] | "?");
            } else if (strcmp(msg_type, "agent_interrupted") == 0) {
                Serial.println("[ws] agent_interrupted");
            }
            break;
        }

        case WStype_BIN:
            // P9: Opus audio frame from server (TTS)
            Serial.printf("[ws] ← binary %d bytes\n", length);
            break;

        case WStype_ERROR:
            Serial.printf("[ws] error: %s\n", payload);
            break;

        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Control message senders (spec §5 device → server)
// ---------------------------------------------------------------------------

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

static void send_session_start() {
    StaticJsonDocument<64> doc;
    doc["type"] = "session_start";
    doc["ts"] = millis() / 1000;
    String out;
    serializeJson(doc, out);
    ws.sendTXT(out);
    Serial.println("[ws] → session_start");
}

static void send_session_end() {
    StaticJsonDocument<64> doc;
    doc["type"] = "session_end";
    doc["ts"] = millis() / 1000;
    String out;
    serializeJson(doc, out);
    ws.sendTXT(out);
    Serial.println("[ws] → session_end");
}

// ---------------------------------------------------------------------------
// Wi-Fi helpers
// ---------------------------------------------------------------------------

static bool wifi_connect(const char* ssid, const char* pass, unsigned long timeout_ms = 30000) {
    Serial.printf("Wi-Fi: connecting to %s... ", ssid);

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > timeout_ms) {
            Serial.println("FAIL (timeout)");
            return false;
        }
        delay(500);
        Serial.print(".");
    }

    Serial.println("OK");
    Serial.print("    IP:      ");
    Serial.println(WiFi.localIP());
    Serial.print("    RSSI:    ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
    return true;
}

// ---------------------------------------------------------------------------
// Boot
// ---------------------------------------------------------------------------

void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(500);

    Serial.println();
    Serial.println("cores3 walkie-talkie boot");

    // --- M5Unified BSP ---
    Serial.print("M5Unified init... ");
    auto cfg = M5.config();
    cfg.serial_baudrate = SERIAL_BAUD;
    M5.begin(cfg);
    Serial.println("OK");
    Serial.printf("Board: %d  Battery: %d%%\n", M5.getBoard(), M5.Power.getBatteryLevel());

    // --- Wi-Fi ---
    if (!wifi_connect(WIFI_SSID, WIFI_PASS)) {
        Serial.println("Wi-Fi failed — retrying in loop");
    } else {
        state = DeviceState::WIFI_CONNECT;
    }

    // --- WebSocket ---
    Serial.printf("WS: connecting to %s:%d/ws/cores3?device_token=%s\n",
                  BACKEND_HOST, BACKEND_PORT, DEVICE_TOKEN);

    ws.begin(BACKEND_HOST, BACKEND_PORT, "/ws/cores3?device_token=" DEVICE_TOKEN);
    ws.onEvent(ws_event_handler);
    ws.setReconnectInterval(WS_RECONNECT_MS);

    Serial.println("setup complete");
}

// ---------------------------------------------------------------------------
// Loop
// ---------------------------------------------------------------------------

void loop() {
    static unsigned long last_heartbeat = 0;
    static unsigned long last_keepalive = 0;
    static bool boot_session_sent = false;
    unsigned long now = millis();

    // --- Wi-Fi reconnect ---
    if (!WiFi.isConnected()) {
        wifi_connect(WIFI_SSID, WIFI_PASS);
    }

    // --- WebSocket service ---
    ws.loop();

    // --- State transitions ---
    if (state == DeviceState::WIFI_CONNECT && WiFi.isConnected()) {
        state = DeviceState::WSS_CONNECT;
        Serial.println("state → WSS_CONNECT");
    }

    // --- Keepalive (every 30s when idle) ---
    if (state == DeviceState::IDLE || state == DeviceState::WSS_CONNECT) {
        if (now - last_keepalive >= KEEPALIVE_MS) {
            last_keepalive = now;
            send_keepalive();
        }
    }

    // --- Quick demo: auto-start session after boot, then repeat ---
    // P13 replaces this with touch PTT
    if (state == DeviceState::IDLE && !boot_session_sent && now > 8000) {
        boot_session_sent = true;
        send_session_start();
        Serial.println("state → SESSION_ACTIVE (auto-demo)");
    }

    // Auto-end after 15s (demo mode)
    if (state == DeviceState::SESSION_ACTIVE && boot_session_sent && now > 23000) {
        send_session_end();
        state = DeviceState::IDLE;
        Serial.println("state → IDLE (demo end)");
    }

    // --- Heartbeat ---
    if (now - last_heartbeat >= HEARTBEAT_MS) {
        last_heartbeat = now;
        Serial.printf("[idle] uptime=%ds wifi=%s rssi=%ddBm heap=%d psram=%d state=%d\n",
                      now / 1000,
                      WiFi.isConnected() ? WiFi.localIP().toString().c_str() : "DOWN",
                      WiFi.RSSI(),
                      ESP.getFreeHeap(),
                      ESP.getFreePsram(),
                      (int)state);
    }

    M5.update();
    delay(10);
}
