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
size_t getArduinoLoopTaskStackSize(void) { return 24576; }

// ---------------------------------------------------------------------------
// State machine
// ---------------------------------------------------------------------------

enum class DeviceState { BOOT, WIFI_CONNECT, WSS_CONNECT, IDLE, SESSION_ACTIVE };
static DeviceState state = DeviceState::BOOT;

// Touch PTT
static bool touch_pressed = false;
static unsigned long touch_down_at = 0;
static const unsigned long TOUCH_DEBOUNCE_MS = 50;
static unsigned long last_tap_at = 0;
static int tap_count = 0;
static const unsigned long DOUBLE_TAP_MS = 400;

// Characters (matching server _CHARACTERS + _VALID_CHARACTERS)
static const char* CHARACTERS[] = {"radek","steve","simba","ryder","creeper","pimpek","crewmate","sonic","pikachu","mario","roblox_noob"};
static const int NUM_CHARS = 11;
static int current_char_idx = 0;  // 0 = radek (default)

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

static void send_switch_character(const char* character) {
    JsonDocument doc;
    doc["type"] = "switch_character";
    doc["character"] = character;
    doc["ts"] = millis() / 1000;
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
            // TTS Opus from server → decode → stream to speaker
            // Single channel, playRaw with stop_current_sound=false so frames
            // queue sequentially instead of overlapping.
            if (state == DeviceState::SESSION_ACTIVE) {
                static int16_t* spk_buf = nullptr;
                if (!spk_buf) spk_buf = (int16_t*)heap_caps_malloc(OPUS_FRAME_SAMPLES * 2, MALLOC_CAP_SPIRAM);
                if (spk_buf) {
                    int samples = opus_decode_frame(payload, len, spk_buf);
                    if (samples > 0) {
                        // channel 0, stop_current=false — queue frames sequentially
                        M5.Speaker.playRaw(spk_buf, samples, OPUS_SAMPLE_RATE, false, 1, 0, false);
                        // Allocate fresh buffer for next frame so previous isn't overwritten
                        spk_buf = (int16_t*)heap_caps_malloc(OPUS_FRAME_SAMPLES * 2, MALLOC_CAP_SPIRAM);
                    }
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

// Tamagotchi-style pixel face renderer
static uint16_t last_bg = 0xFFFF;  // force initial draw

static void draw_face( uint16_t bg) {
    // Only full-redraw on state change
    static DeviceState last_state = DeviceState::BOOT;
    if (state == last_state && bg == last_bg) return;
    last_state = state; last_bg = bg;

    M5.Lcd.fillScreen(bg);

    int cx = 160, cy = 80;  // face center (landscape 320x240, rotated)

    // Draw circular face background
    M5.Lcd.fillCircle(cx, cy, 55, TFT_BLACK);
    M5.Lcd.drawCircle(cx, cy, 55, TFT_WHITE);

    // Eyes — two white circles with black pupils that move based on state
    int eye_y = cy - 15;
    int eye_spacing = 22;
    int pupil_offset = (state == DeviceState::SESSION_ACTIVE) ? 5 : 0;

    M5.Lcd.fillCircle(cx - eye_spacing, eye_y, 12, TFT_WHITE);
    M5.Lcd.fillCircle(cx + eye_spacing, eye_y, 12, TFT_WHITE);
    M5.Lcd.fillCircle(cx - eye_spacing + pupil_offset, eye_y + 2, 5, TFT_BLACK);
    M5.Lcd.fillCircle(cx + eye_spacing + pupil_offset, eye_y + 2, 5, TFT_BLACK);

    // Mouth — changes expression by state
    int mouth_y = cy + 20;
    if (state == DeviceState::SESSION_ACTIVE) {
        // Talking: open circle mouth
        M5.Lcd.fillCircle(cx, mouth_y + 5, 10, TFT_RED);
    } else if (state == DeviceState::WSS_CONNECT || state == DeviceState::WIFI_CONNECT) {
        // Connecting: flat line
        M5.Lcd.drawLine(cx - 12, mouth_y, cx + 12, mouth_y, TFT_WHITE);
    } else {
        // Idle: happy smile
        M5.Lcd.fillRect(cx - 12, mouth_y, 24, 6, TFT_WHITE);
        M5.Lcd.fillRect(cx - 8, mouth_y - 4, 16, 4, bg);  // erase center
    }

    // Ears (small circles at top sides)
    M5.Lcd.fillCircle(cx - 52, cy - 25, 10, TFT_BLACK);
    M5.Lcd.drawCircle(cx - 52, cy - 25, 10, TFT_WHITE);
    M5.Lcd.fillCircle(cx + 52, cy - 25, 10, TFT_BLACK);
    M5.Lcd.drawCircle(cx + 52, cy - 25, 10, TFT_WHITE);

    // Character name below face
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(TFT_WHITE, bg);
    M5.Lcd.setCursor(cx - 30, cy + 65);
    M5.Lcd.println(CHARACTERS[current_char_idx]);

    // Status bar at bottom
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(TFT_DARKGREY, bg);
    M5.Lcd.setCursor(5, 228);
    if (WiFi.isConnected())
        M5.Lcd.printf("wifi:%ddBm batt:%d%%", WiFi.RSSI(), M5.Power.getBatteryLevel());
    else
        M5.Lcd.print("wifi:DOWN");
}

static void draw_ui() {
    uint16_t bg;
    switch (state) {
        case DeviceState::SESSION_ACTIVE: bg = TFT_RED;     break;
        case DeviceState::WSS_CONNECT:    bg = TFT_BLUE;    break;
        case DeviceState::WIFI_CONNECT:   bg = TFT_ORANGE;  break;
        default:                          bg = TFT_DARKGREEN;break;
    }
    draw_face(bg);
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
    cfg.internal_mic = true;
    cfg.internal_spk = true;
    M5.begin(cfg);
    // Configure both at 16kHz before use
    {
        auto spk_cfg = M5.Speaker.config();
        spk_cfg.sample_rate = OPUS_SAMPLE_RATE;
        M5.Speaker.config(spk_cfg);
        auto mic_cfg = M5.Mic.config();
        mic_cfg.sample_rate = OPUS_SAMPLE_RATE;
        M5.Mic.config(mic_cfg);
    }
    // Start in speaker mode (idle: ready to hear agent)
    M5.Mic.end();
    M5.Speaker.begin();
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
            touch_down_at = now;
            // Double-tap detection for character switching
            if (now - last_tap_at < DOUBLE_TAP_MS) {
                tap_count++;
                if (tap_count >= 2 && state == DeviceState::IDLE) {
                    current_char_idx = (current_char_idx + 1) % NUM_CHARS;
                    send_switch_character(CHARACTERS[current_char_idx]);
                    Serial.printf("→ character: %s\n", CHARACTERS[current_char_idx]);
                    tap_count = 0;
                }
            } else {
                tap_count = 1;
                // Single tap + hold = PTT
                touch_pressed = true;
                if (state == DeviceState::IDLE) {
                    M5.Speaker.end();
                    M5.Mic.begin();
                    send_session_start();
                    Serial.println("→ SESSION_ACTIVE (PTT)");
                }
            }
            last_tap_at = now;
        }
    }

    if (touch.wasReleased() && touch_pressed) {
        touch_pressed = false;
        if (state == DeviceState::SESSION_ACTIVE) {
            send_session_end();
            state = DeviceState::IDLE;
            M5.Mic.end();
            M5.Speaker.begin();
            Serial.println("→ IDLE (PTT release)");
        }
    }

    // Audio: record mic → Opus encode → WS send (during PTT hold)
    static int16_t* mic_buf = nullptr;
    static uint8_t* opus_out = nullptr;
    if (!mic_buf) {
        mic_buf = (int16_t*)heap_caps_malloc(OPUS_FRAME_SAMPLES * 2, MALLOC_CAP_SPIRAM);
        opus_out = (uint8_t*)heap_caps_malloc(128, MALLOC_CAP_SPIRAM);
    }
    if (state == DeviceState::SESSION_ACTIVE && touch_pressed && M5.Mic.isEnabled() && mic_buf && opus_out) {
        size_t recorded = M5.Mic.record(mic_buf, OPUS_FRAME_SAMPLES, OPUS_SAMPLE_RATE);
        if (recorded == OPUS_FRAME_SAMPLES) {
            int pkt_len = opus_encode_frame(mic_buf, opus_out, 128);
            if (pkt_len > 0) {
                ws.sendBIN(opus_out, pkt_len);
            }
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
