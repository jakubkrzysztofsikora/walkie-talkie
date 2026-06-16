/**
 * CoreS3 Walkie-Talkie — main entry point
 *
 * P7:  M5Unified BSP + Wi-Fi
 * P8:  WebSocket client + control protocol + state machine
 * P9:  Opus codec
 * P13: Touchscreen PTT + color-coded status display
 * P10: Audio I2S (deferred — needs FreeRTOS task pinning)
 */

#include <Arduino.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>

#include "config/secrets.h"
#include "audio/opus_stub.h"

// Override default 8KB loopTask stack
size_t getArduinoLoopTaskStackSize(void) { return 16384; }

// ---------------------------------------------------------------------------
// State machine
// ---------------------------------------------------------------------------

enum class DeviceState { BOOT, WIFI_CONNECT, WSS_CONNECT, IDLE, SESSION_ACTIVE };
static DeviceState state = DeviceState::BOOT;

// Touch PTT
static bool touch_pressed = false;
static unsigned long touch_down_at = 0;
static const unsigned long TOUCH_DEBOUNCE_MS = 50;

// WebSocket
static WebSocketsClient ws;

// ---------------------------------------------------------------------------
// WebSocket helpers
// ---------------------------------------------------------------------------

static void send_json(const char* type) {
    JsonDocument doc;
    doc["type"] = type;
    doc["ts"] = millis() / 1000;
    String out; serializeJson(doc, out); ws.sendTXT(out);
}

static void send_keepalive() {
    JsonDocument doc;
    doc["type"] = "keepalive"; doc["ts"] = millis() / 1000;
    String out; serializeJson(doc, out); ws.sendTXT(out);
}

static void send_session_start() {
    JsonDocument doc;
    doc["type"] = "session_start"; doc["ts"] = millis() / 1000;
    String out; serializeJson(doc, out); ws.sendTXT(out);
}

static void send_session_end() {
    JsonDocument doc;
    doc["type"] = "session_end"; doc["ts"] = millis() / 1000;
    String out; serializeJson(doc, out); ws.sendTXT(out);
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
            JsonDocument doc;
            if (deserializeJson(doc, payload, len)) return;
            const char* t = doc["type"] | "";
            Serial.printf("[ws] <- %s\n", t);
            if (!strcmp(t, "hello") && state == DeviceState::WSS_CONNECT)
                state = DeviceState::IDLE;
            else if (!strcmp(t, "session_started"))
                state = DeviceState::SESSION_ACTIVE;
            else if (!strcmp(t, "session_ended"))
                state = DeviceState::IDLE;
            else if (!strcmp(t, "agent_interrupted"))
                {} // P10: flush jitter buffer
            break;
        }
        case WStype_BIN:
            // TTS Opus from server → decode → speaker
            if (state == DeviceState::SESSION_ACTIVE) {
                int16_t pcm[OPUS_FRAME_SAMPLES];
                int samples = opus_decode_frame(payload, len, pcm);
                if (samples > 0) M5.Speaker.playRaw(pcm, samples, OPUS_SAMPLE_RATE, false);
            }
            break;
        default: break;
    }
}

// ---------------------------------------------------------------------------
// Wi-Fi
// ---------------------------------------------------------------------------

static bool wifi_connect(const char* ssid, const char* pass, unsigned long timeout_ms = 30000) {
    Serial.printf("WiFi: %s... ", ssid);
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
// Display
// ---------------------------------------------------------------------------

static void draw_ui() {
    // Background color signals state
    uint16_t bg;
    const char* label;
    switch (state) {
        case DeviceState::SESSION_ACTIVE: bg = TFT_RED;    label = "TALKING";   break;
        case DeviceState::WSS_CONNECT:    bg = TFT_BLUE;   label = "CONNECTING";break;
        case DeviceState::WIFI_CONNECT:   bg = TFT_ORANGE; label = "WIFI...";   break;
        default:                          bg = TFT_DARKGREEN; label = "GOTOWY"; break;
    }

    M5.Lcd.fillScreen(bg);

    // Main label
    M5.Lcd.setTextColor(TFT_WHITE, bg);
    M5.Lcd.setTextSize(3);
    M5.Lcd.setCursor(20, 30);
    M5.Lcd.println(label);

    // IP + RSSI
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(TFT_WHITE, bg);
    M5.Lcd.setCursor(20, 70);
    if (WiFi.isConnected())
        M5.Lcd.printf("IP: %s  %ddBm", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    else
        M5.Lcd.print("WiFi: DOWN");

    // Battery
    M5.Lcd.setCursor(20, 85);
    M5.Lcd.printf("Batt: %d%%  Heap: %dK", M5.Power.getBatteryLevel(), ESP.getFreeHeap() / 1024);

    // PTT hint
    if (state == DeviceState::IDLE) {
        M5.Lcd.setTextSize(2);
        M5.Lcd.setTextColor(TFT_WHITE, bg);
        M5.Lcd.setCursor(20, 110);
        M5.Lcd.println("Nacisnij i mow");
    }

    // Character
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(TFT_CYAN, bg);
    M5.Lcd.setCursor(20, 140);
    M5.Lcd.println("Radek");
}

// ---------------------------------------------------------------------------
// Boot
// ---------------------------------------------------------------------------

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\ncores3 walkie-talkie boot");

    auto cfg = M5.config();
    cfg.serial_baudrate = 115200;
    cfg.internal_mic = false;  // P10
    cfg.internal_spk = false;
    M5.begin(cfg);
    Serial.printf("Board:%d Battery:%d%%\n", M5.getBoard(), M5.Power.getBatteryLevel());

    M5.Lcd.setRotation(1);
    M5.Lcd.fillScreen(TFT_BLACK);
    M5.Lcd.setTextColor(TFT_GREEN, TFT_BLACK);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(20, 30);
    M5.Lcd.println("Walkie-Talkie");
    M5.Lcd.setTextSize(1);
    M5.Lcd.setCursor(20, 55);
    M5.Lcd.println("CoreS3 boot...");

    if (!opus_stub_init())
        Serial.println("[opus] FAILED");

    if (!wifi_connect(WIFI_SSID, WIFI_PASS))
        Serial.println("WiFi FAILED");
    else
        state = DeviceState::WIFI_CONNECT;

    ws.begin(BACKEND_HOST, BACKEND_PORT, "/ws/cores3?device_token=" DEVICE_TOKEN);
    ws.onEvent(ws_handler);
    ws.setReconnectInterval(5000);

    Serial.println("setup complete");
}

// ---------------------------------------------------------------------------
// Loop
// ---------------------------------------------------------------------------

void loop() {
    static unsigned long last_beat = 0, last_keepalive = 0, last_display = 0;
    unsigned long now = millis();

    if (!WiFi.isConnected()) wifi_connect(WIFI_SSID, WIFI_PASS);
    ws.loop();

    if (state == DeviceState::WIFI_CONNECT && WiFi.isConnected())
        { state = DeviceState::WSS_CONNECT; }

    // Keepalive
    if ((state == DeviceState::IDLE || state == DeviceState::WSS_CONNECT) && now - last_keepalive >= 30000) {
        last_keepalive = now; send_keepalive();
    }

    // --- Touch PTT ---
    M5.update();
    auto touch = M5.Touch.getDetail();

    if (touch.wasPressed()) {
        if (now - touch_down_at > TOUCH_DEBOUNCE_MS) {
            touch_pressed = true;
            touch_down_at = now;
            if (state == DeviceState::IDLE) {
                send_session_start();
                Serial.println("→ SESSION_ACTIVE (PTT)");
            }
        }
    }

    if (touch.wasReleased() && touch_pressed) {
        touch_pressed = false;
        if (state == DeviceState::SESSION_ACTIVE) {
            send_session_end();
            state = DeviceState::IDLE;
            Serial.println("→ IDLE (PTT release)");
        }
    }

    // --- Display update ---
    if (now - last_display >= 1000) {
        last_display = now;
        draw_ui();
    }

    // Heartbeat
    if (now - last_beat >= 10000) {
        last_beat = now;
        Serial.printf("[idle] uptime=%ds wifi=%s rssi=%ddBm heap=%d psram=%d state=%d\n",
                      now / 1000,
                      WiFi.isConnected() ? WiFi.localIP().toString().c_str() : "DOWN",
                      WiFi.RSSI(), ESP.getFreeHeap(), ESP.getFreePsram(), (int)state);
    }
}
