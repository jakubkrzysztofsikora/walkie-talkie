/**
 * CoreS3 Walkie-Talkie
 * Gen Alpha UI: pixel mascot, landscape, char picker, icon-only, no text.
 * SAFE_MODE: speaker-only, no I2S switching (mic disabled).
 */
#include <Arduino.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <esp_system.h>   // esp_reset_reason()

#include "config/secrets.h"
#include "audio/opus_stub.h"
#include "audio/audio_hal.h"
#include "audio/audio_task.h"

// Single source of truth for the character roster + themes (drops the old inline
// CHARS[]/THEMES[]/NCHARS that drifted to 11 vs the lib's 12).
#include "walkie_ui_logic/sprites.h"
#include "walkie_ui_logic/animator.h"
#ifdef UI_ENGINE
#include "ui/ui_engine.h"
#endif
// PXA1 pixel-animation overlay (PSRAM storage + masked blit). pixel_anim_set /
// pixel_anim_active compile in both builds; the blit is UI_ENGINE-only.
#include "ui/pixel_anim.h"
#include "walkie_ui_logic/pixel_anim.h"

// Override default 8KB loopTask stack (UI + WS + Opus needs ~28KB)
size_t getArduinoLoopTaskStackSize(void) { return 28672; }

// --- State ---
enum DeviceState { BOOT, WIFI_CONNECT, WSS_CONNECT, IDLE, SESSION_ACTIVE };
static DeviceState state = BOOT;
static bool g_audio_ok = false;   // false if codec/I2S/task bring-up failed (device silent)

// --- Audio half-duplex mode (hard-gate: mic and speaker never active together) ---
// Invariant: mic capture is enabled IFF mode==AUDIO_TALK. Inbound TTS forces
// AUDIO_LISTEN (and disables the mic first), so a TTS frame arriving while the
// PTT is still held can never leave the mic hot recording speaker echo.
enum AudioMode { AUDIO_IDLE, AUDIO_TALK, AUDIO_LISTEN };
static AudioMode amode = AUDIO_IDLE;

static void audio_set_mode(AudioMode m) {
    if (m == amode) return;
    // Entering TALK: flush any residual TTS PCM BEFORE arming the mic so the
    // speaker can't still be draining into an open mic.
    if (m == AUDIO_TALK) audio_task_stop_output();
    // Only commit the logical mode if the (non-starvable) control command was
    // accepted. On a false return the mic state is unchanged, so leaving amode
    // as-is keeps the gate honest rather than desyncing from the real mic.
    if (audio_task_set_mic_enabled(m == AUDIO_TALK)) {
        amode = m;
    } else {
        Serial.println("[audio] WARN mic gate command rejected — mode unchanged");
    }
}

// --- Touch ---
static bool touch_pressed = false;
static unsigned long touch_down_at = 0;
static const unsigned long LONG_PRESS_MS = 1500;
static bool menu_open = false;

// --- Characters ---
// Roster + themes now come from the walkie_ui lib (one source of truth). The
// backend roster is 11 agents; the lib must agree or send_switch(idx) could read
// an out-of-bounds id.
static const int NCHARS = (int)walkie_ui::NCHARS;
static_assert(walkie_ui::NCHARS == 11, "character roster must be 11 (backend agents)");
static int char_idx = 0;

// Character id string for the switch_character message (replaces the old CHARS[]).
static const char* char_id(int i) {
    const walkie_ui::CharacterTheme* t = walkie_ui::get_character_theme((size_t)i);
    return t ? t->id : "radek";
}

// --- Theme (runtime accent/bg pulled from the lib theme table) ---
static uint16_t ui_accent = 0x07FF, ui_bg = 0x10E2;

// --- WebSocket ---
static WebSocketsClient ws;

static void send_session_end();   // fwd decl: ws_handler uses it (fast-tap race close)

static void ws_handler(WStype_t type, uint8_t* payload, size_t len) {
    switch (type) {
        case WStype_DISCONNECTED:
            if (state == SESSION_ACTIVE) state = WSS_CONNECT;
            touch_pressed = false;        // avoid stale held-state → spurious menu on reconnect
            audio_set_mode(AUDIO_IDLE);   // never leave the mic hot on a drop
            break;
        case WStype_CONNECTED:
            Serial.println("[ws] connected"); break;
        case WStype_TEXT: {
            JsonDocument doc;
            if (deserializeJson(doc, payload, len)) return;
            const char* t = doc["type"] | "";
            if (!strcmp(t, "hello") && (state == WIFI_CONNECT || state == WSS_CONNECT)) state = IDLE;
            else if (!strcmp(t, "session_started") && state != SESSION_ACTIVE) {
                // Guard on !SESSION_ACTIVE: a duplicate/late session_started must
                // not re-arm TALK while TTS from the current turn is still draining.
                state = SESSION_ACTIVE;
                // Only start capturing if the finger is still down. If the user
                // already released before the server ack arrived (fast-tap race),
                // close the session immediately instead of stranding it active.
                if (touch_pressed) audio_set_mode(AUDIO_TALK);
                else { send_session_end(); state = IDLE; audio_set_mode(AUDIO_IDLE); }
            }
            else if (!strcmp(t, "session_ended")) { state = IDLE; touch_pressed = false; audio_set_mode(AUDIO_IDLE); }
            break;
        }
        case WStype_BIN:
            // A WStype_BIN message is EITHER a PXA1 pixel animation OR an Opus
            // TTS frame. Disambiguate by the 4-byte magic FIRST: PXA1 payloads
            // are >=8 bytes and start with "PXA1"; Opus frames are small and
            // never do. On a match, store the animation (PSRAM copy) and stop —
            // do NOT fall through to opus_decode (which would garble it).
            // NOTE: a real Opus frame whose first 4 bytes happen to equal "PXA1"
            // (chance ~2^-32) would be MISTAKEN for an animation and dropped here,
            // never decoded. That's an acceptable one-in-four-billion audio glitch,
            // not a crash — pixel_anim_set then rejects it as a malformed header.
            if (walkie_ui::pxa_has_magic(payload, len)) {
                pixel_anim_set(payload, len);   // validates internally; no-op if malformed
                break;
            }
            // TTS: decode Opus → queue for playback. Play whenever we are NOT
            // actively capturing (TALK). This lets the agent's reply — which the
            // backend streams AFTER the user releases PTT — and any connect-time
            // greeting actually play, instead of being dropped because the device
            // already left SESSION_ACTIVE on release. The hard-gate is preserved:
            // we force LISTEN (mic off) before queuing any audio, so the mic is
            // never hot during playback. Only TALK (user holding PTT) suppresses
            // playback, which is correct half-duplex behaviour.
            if (amode != AUDIO_TALK && (state == IDLE || state == SESSION_ACTIVE)) {
                if (amode != AUDIO_LISTEN) audio_set_mode(AUDIO_LISTEN);
                if (amode == AUDIO_LISTEN) {   // mic confirmed off before we play
                    int16_t buf[OPUS_FRAME_SAMPLES];
                    int n = opus_decode_frame(payload, len, buf);
                    if (n > 0) audio_task_play_pcm(buf, n);
                }
            }
            break;
        default: break;
    }
}

static void send_json(const char* type) { JsonDocument d; d["type"]=type; d["ts"]=millis()/1000; String o; serializeJson(d,o); ws.sendTXT(o); }
static void send_keepalive() { JsonDocument d; d["type"]="keepalive";d["ts"]=millis()/1000; String o; serializeJson(d,o); ws.sendTXT(o); }
static void send_session_start() { send_json("session_start"); }
static void send_session_end() { send_json("session_end"); }
static void send_switch(const char* c) { JsonDocument d; d["type"]="switch_character"; d["character"]=c; d["ts"]=millis()/1000; String o; serializeJson(d,o); ws.sendTXT(o); }

// --- WiFi ---
static bool wifi_connect(const char* ssid, const char* pass, unsigned long to=30000) {
    Serial.printf("WiFi: %s... ", ssid);
    WiFi.mode(WIFI_STA); WiFi.begin(ssid, pass);
    unsigned long s = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - s > to) { Serial.println("FAIL"); return false; }
        delay(500); Serial.print(".");
    }
    Serial.printf("OK IP=%s RSSI=%d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    return true;
}

// ==================================================================
// DISPLAY — Gen Alpha pixel-art mascot, landscape, icon-only
// ==================================================================
static const int MX = 160, MY = 75, PX = 8;

// Sprites
static const uint8_t FACE[8] = {0b00111100,0b01111110,0b11111111,0b11111111,0b11011011,0b11111111,0b01111110,0b00111100};
static const uint8_t BLINK[8] = {0b00111100,0b01111110,0b11000011,0b10000001,0b10000001,0b11000011,0b01111110,0b00111100};
static const uint8_t HAPPY[8] = {0b00111100,0b01111110,0b11111111,0b11111111,0b11000011,0b11100111,0b01111110,0b00111100};
static const uint8_t WOW[8]   = {0b00111100,0b01111110,0b11100111,0b11100111,0b11111111,0b11100111,0b01111110,0b00111100};

static int anim_f = 0, blink_s = 0, mouth_f = 0;
static unsigned long anim_t = 0;

// Icons (12x12 px)
static void icon_mic(int x, int y, uint16_t c) {
    M5.Lcd.fillRoundRect(x+3,y,6,8,2,c); M5.Lcd.fillRect(x+4,y+8,4,3,c); M5.Lcd.fillRect(x+5,y+11,2,2,c);
}
static void icon_batt(int x, int y, uint16_t c, int pct) {
    M5.Lcd.drawRect(x,y,14,8,c); M5.Lcd.fillRect(x+14,y+2,3,4,c);
    int w=(pct*12+50)/100; if(w>12)w=12;
    uint16_t bc=(pct<20)?0xF800:(pct<50)?0xFFE0:0x07E0;
    M5.Lcd.fillRect(x+1,y+1,w,6,bc);
}
static void icon_wifi(int x, int y, uint16_t c, bool on) {
    if(on){M5.Lcd.fillCircle(x+5,y+8,2,c);M5.Lcd.drawArc(x+5,y+8,5,7,225,315,c);M5.Lcd.drawArc(x+5,y+8,8,10,225,315,c);}
    else{M5.Lcd.drawLine(x,y,x+10,y+10,0xF800);M5.Lcd.drawLine(x+10,y,x,y+10,0xF800);}
}
static void star(int x, int y, uint16_t c, bool b) {
    if(b){M5.Lcd.fillRect(x-1,y-1,3,3,c);M5.Lcd.drawFastHLine(x-2,y,5,c);M5.Lcd.drawFastVLine(x,y-2,5,c);}
    else M5.Lcd.fillRect(x,y,2,2,c);
}
static void cloud(int x, int y, uint16_t c) {
    M5.Lcd.fillRect(x+4,y,12,6,c);M5.Lcd.fillRect(x,y+4,20,4,c);M5.Lcd.fillRect(x+8,y-2,8,4,c);
}
static void ground(int y, uint16_t c) {
    for(int x=0;x<320;x+=16){int h=4+((x/16)%3)*4;M5.Lcd.fillRect(x,y-h,16,h+2,c);}
}
static void spr(int cx, int cy, const uint8_t s[8], uint16_t c) {
    for(int y=0;y<8;y++)for(int x=0;x<8;x++)if(s[y]&(1<<(7-x)))M5.Lcd.fillRect(cx-32+x*PX,cy-32+y*PX,PX,PX,c);
}
static void wave(int cx, int y, int n, int f, uint16_t c) {
    for(int i=0;i<n;i++){int h=4+((f+i*3)%8)*2;M5.Lcd.fillRect(cx-n*5+i*10,y-h,6,h,c);}
}
static void gear(int cx, int cy, int r, int f, uint16_t c) {
    float a=f*0.3f;for(int i=0;i<4;i++){float aa=a+i*1.57f;M5.Lcd.fillCircle(cx+(int)(cos(aa)*r),cy+(int)(sin(aa)*r),2,c);}
    M5.Lcd.drawCircle(cx,cy,r,c);
}

static void draw_mascot() {
    uint16_t rc = ui_accent;
    // Glow ring
    int rr = 52;
    if(state==SESSION_ACTIVE){M5.Lcd.drawCircle(MX,MY,rr,rc);M5.Lcd.drawCircle(MX,MY,rr-1,rc);}
    else if(state==IDLE&&anim_f%12<6)M5.Lcd.drawCircle(MX,MY,rr,rc);
    // Card
    M5.Lcd.fillRoundRect(MX-42,MY-18,84,80,10,0x6B4D);
    M5.Lcd.fillRoundRect(MX-39,MY-15,78,74,8,ui_bg);
    // Face
    const uint8_t* fa = FACE;
    if(blink_s==2)fa=BLINK;
    else if(state==SESSION_ACTIVE&&mouth_f>0)fa=HAPPY;
    else if(state==WIFI_CONNECT||state==WSS_CONNECT)fa=WOW;
    spr(MX,MY,fa,ui_accent);
    // Mouth
    if(state==SESSION_ACTIVE&&mouth_f>0){int mw=6+mouth_f*5;M5.Lcd.fillRect(MX-mw,MY+5*PX,mw*2,3+mouth_f*2,ui_bg);M5.Lcd.drawRect(MX-mw,MY+5*PX,mw*2,3+mouth_f*2,rc);}
}

static void draw_menu() {
    M5.Lcd.fillScreen(0x0000);
    int cw=53, rh=80;
    M5.Lcd.fillRect(0,0,320,rh,ui_accent);
    M5.Lcd.setTextSize(2);M5.Lcd.setTextColor(0xFFFF,ui_accent);
    M5.Lcd.setCursor(65,rh/2-12);M5.Lcd.print("PICK HERO");
    for(int i=0;i<NCHARS;i++){
        const walkie_ui::CharacterTheme* th=walkie_ui::get_character_theme((size_t)i);
        uint16_t acc=th?th->accent:0x07FF;
        int cx=(i%6)*cw+cw/2,cy=rh+(i/6)*rh+rh/2,r=(i==char_idx)?24:20;
        if(i==char_idx)M5.Lcd.fillCircle(cx,cy,r+2,0xFFFF);
        M5.Lcd.fillCircle(cx,cy,r,acc);
        M5.Lcd.setTextSize(1);M5.Lcd.setTextColor(0xFFFF,acc);
        char nm[2]={(char)toupper(char_id(i)[0]),0};
        M5.Lcd.setCursor(cx-4,cy-6);M5.Lcd.print(nm);
    }
}

static void draw_full() {
    if(menu_open){draw_menu();return;}
    M5.Lcd.fillScreen(ui_bg);
    uint16_t gc=(ui_bg>>1)&0x7BEF, sc=(ui_accent>>2)&0x39E7;
    for(int i=0;i<12;i++)star(10+i*27,12+(i%3)*10,sc,i%3==0);
    cloud(20,30,sc);cloud(240,18,sc);ground(195,gc);
    icon_batt(4,4,ui_accent,M5.Power.getBatteryLevel());
    icon_wifi(300,2,ui_accent,WiFi.isConnected());
    draw_mascot();
    // Char picker dots
    int cy=175;for(int i=-1;i<=1;i++){int idx=(char_idx+i+NCHARS)%NCHARS;int r=(i==0)?8:5;M5.Lcd.fillCircle(MX+i*28,cy,r,(i==0)?ui_accent:0x6B4D);if(i==0)M5.Lcd.fillCircle(MX,cy,r-2,ui_bg);}
    M5.Lcd.fillTriangle(MX-18,cy,MX-10,cy-4,MX-10,cy+4,0x6B4D);
    M5.Lcd.fillTriangle(MX+18,cy,MX+10,cy-4,MX+10,cy+4,0x6B4D);
    M5.Lcd.setTextSize(1);M5.Lcd.setTextColor(0x6B4D,ui_bg);M5.Lcd.setCursor(MX-35,170);M5.Lcd.print("hold 2s");
    // Bottom
    int by=215;
    if(state==SESSION_ACTIVE)wave(MX,by-10,8,anim_f,ui_accent);
    else if(state==WIFI_CONNECT||state==WSS_CONNECT)gear(MX,by-10,14,anim_f,ui_accent);
    int mx=154,my=by;
    if(state==SESSION_ACTIVE){icon_mic(mx-4,my-8,0xF800);M5.Lcd.drawCircle(mx+2,my+2,22,0xF800);M5.Lcd.drawCircle(mx+2,my+2,21,0xF800);}
    else if(state==IDLE){bool p=(anim_f%12<6);icon_mic(mx-4,my-8,p?ui_accent:0x6B4D);if(p)M5.Lcd.drawCircle(mx+2,my+2,22,ui_accent);}
    else icon_mic(mx-4,my-8,0x6B4D);
}

static void draw_delta() {
    if(menu_open)return;
    int by=215;
    if(state==SESSION_ACTIVE){M5.Lcd.fillRect(MX-45,by-24,90,20,ui_bg);wave(MX,by-10,8,anim_f,ui_accent);}
    if(state==IDLE){M5.Lcd.fillCircle(MX,by+2,24,ui_bg);bool p=(anim_f%12<6);icon_mic(154-4,by-8,p?ui_accent:0x6B4D);if(p)M5.Lcd.drawCircle(156,by+2,22,ui_accent);}
    M5.Lcd.fillRect(MX-16,MY-12,32,16,ui_bg);
    if(blink_s==2){M5.Lcd.drawFastHLine(MX-12,MY-6,10,ui_accent);M5.Lcd.drawFastHLine(MX+2,MY-6,10,ui_accent);}
    else{int pr=(state==SESSION_ACTIVE)?4:3;M5.Lcd.fillCircle(MX-10,MY-4,pr,ui_accent);M5.Lcd.fillCircle(MX+10,MY-4,pr,ui_accent);}
}

static void draw_ui() {
    static DeviceState ls = BOOT;
    static int lc = -1, lm = -1, lb = -1;
    if(millis()-anim_t>=150){anim_f=(anim_f+1)%32;anim_t=millis();if(anim_f==0)blink_s=(blink_s+1)%3;}
    bool sc=(state!=ls);ls=state;
    if(char_idx!=lc){lc=char_idx;const walkie_ui::CharacterTheme* th=walkie_ui::get_character_theme((size_t)(char_idx%NCHARS));if(th){ui_accent=th->accent;ui_bg=th->background;}sc=true;}
    if(sc)draw_full();else draw_delta();
}

// ==================================================================
// UI ENGINE (feature-flagged sprite renderer; inline draw_ui() is the
// known-good fallback and stays until a later cutover commit)
// ==================================================================
#ifdef UI_ENGINE
static walkie_ui::UIEngine g_ui;
static bool g_ui_ok = false;

// Enum-drift guards: the lib mirrors these enums by VALUE (the switches below map
// by name, but other paths cast int<->enum). If anyone reorders either side these
// fire at compile time. Keep in lockstep with animator.h's ScreenState/AudioMode.
static_assert((int)BOOT           == (int)walkie_ui::ScreenState::BOOT,           "ScreenState BOOT drift");
static_assert((int)WIFI_CONNECT   == (int)walkie_ui::ScreenState::WIFI_CONNECT,   "ScreenState WIFI_CONNECT drift");
static_assert((int)WSS_CONNECT    == (int)walkie_ui::ScreenState::WSS_CONNECT,    "ScreenState WSS_CONNECT drift");
static_assert((int)IDLE           == (int)walkie_ui::ScreenState::IDLE,           "ScreenState IDLE drift");
static_assert((int)SESSION_ACTIVE == (int)walkie_ui::ScreenState::SESSION_ACTIVE, "ScreenState SESSION_ACTIVE drift");
static_assert((int)AUDIO_IDLE     == (int)walkie_ui::AudioMode::AUDIO_IDLE,       "AudioMode AUDIO_IDLE drift");
static_assert((int)AUDIO_TALK     == (int)walkie_ui::AudioMode::AUDIO_TALK,       "AudioMode AUDIO_TALK drift");
static_assert((int)AUDIO_LISTEN   == (int)walkie_ui::AudioMode::AUDIO_LISTEN,     "AudioMode AUDIO_LISTEN drift");

// Translate main.cpp's local enums into the lib's mirror enums.
// NOTE: these switches intentionally have NO default case so adding a new
// enumerator triggers a -Wswitch warning (catches drift the static_asserts above
// can't — a new state with no mapping).
static walkie_ui::ScreenState lib_screen(DeviceState s) {
    switch (s) {
        case BOOT:           return walkie_ui::ScreenState::BOOT;
        case WIFI_CONNECT:   return walkie_ui::ScreenState::WIFI_CONNECT;
        case WSS_CONNECT:    return walkie_ui::ScreenState::WSS_CONNECT;
        case IDLE:           return walkie_ui::ScreenState::IDLE;
        case SESSION_ACTIVE: return walkie_ui::ScreenState::SESSION_ACTIVE;
    }
    return walkie_ui::ScreenState::IDLE;
}
static walkie_ui::AudioMode lib_amode(AudioMode m) {
    switch (m) {
        case AUDIO_IDLE:   return walkie_ui::AudioMode::AUDIO_IDLE;
        case AUDIO_TALK:   return walkie_ui::AudioMode::AUDIO_TALK;
        case AUDIO_LISTEN: return walkie_ui::AudioMode::AUDIO_LISTEN;
    }
    return walkie_ui::AudioMode::AUDIO_IDLE;
}

// Push current device/audio state into the engine and render one frame.
// noinline so ui_engine_frame stays a distinct symbol in firmware.elf (lets `nm`
// confirm the UI_ENGINE path is linked, not inlined into loop()).
static void __attribute__((noinline)) ui_engine_frame() {
    if (!g_ui_ok) { draw_ui(); return; }   // PSRAM alloc failed → safe fallback
    walkie_ui::ui_engine_set_state(g_ui, lib_screen(state));
    walkie_ui::ui_engine_set_character(g_ui, (size_t)char_idx);
    walkie_ui::ui_engine_set_menu_open(g_ui, menu_open);
    walkie_ui::ui_engine_set_ptt(g_ui, touch_pressed);
    // Feed a coarse speaking level so the waveform/mouth only animate when the
    // agent is actually playing audio. This is an on/off proxy (mic-gate says
    // LISTEN == TTS draining), NOT a true amplitude — good enough to stop the
    // waveform free-running on mouth_frame during silence. A future improvement
    // would derive a real level from playback-queue activity.
    uint8_t level = (amode == AUDIO_LISTEN) ? 2 : 0;
    walkie_ui::ui_engine_set_speaking_level(g_ui, level);
    // Suppress the PXA1 overlay blit while the user is actively talking so the
    // masked-run blit can't steal time from the mic uplink (protects out_drop).
    walkie_ui::ui_engine_set_overlay_suppressed(g_ui, amode == AUDIO_TALK);
    // Drive the expression from the pure decision function (keyed on amode so the
    // agent-speaking face shows whenever TTS plays, including post-release IDLE).
    walkie_ui::Expression expr =
        walkie_ui::compute_screen(lib_amode(amode), lib_screen(state), touch_pressed, menu_open);
    walkie_ui::ui_engine_set_expression(g_ui, expr);
    walkie_ui::ui_engine_render(g_ui, M5.Power.getBatteryLevel(), WiFi.isConnected());
}
#endif

// ==================================================================
// SETUP
// ==================================================================
void setup() {
    Serial.begin(115200);delay(500);
    Serial.println("\ncores3 walkie-talkie boot");
    // Reset reason: a brown-out (amp inrush) reports as rst:0x3 in the ROM string
    // but esp_reset_reason() gives the true cause. Soak/stress watch this for
    // ESP_RST_BROWNOUT(=9) / ESP_RST_TASK_WDT(=11) / ESP_RST_PANIC(=4).
    Serial.printf("[boot] reset_reason=%d\n", (int)esp_reset_reason());
    auto cfg = M5.config();
    cfg.serial_baudrate=115200;cfg.internal_mic=true;cfg.internal_spk=true;
    M5.begin(cfg);
    // NOTE: keep cfg.internal_* (board detection + In_I2C bring-up) but do NOT
    // call M5.Speaker.begin()/M5.Mic.begin(). The raw audio_hal owns I2S0 and the
    // codec I2C bring-up; M5Unified's audio path would install I2S1 on the SAME
    // pins and fight us. (See plan Phase 2.)
#ifdef UI_ENGINE
    // Bring up the sprite engine once. The 150 KB back-buffer goes to PSRAM
    // (setPsram inside ui_engine_init); log heap/psram around it so an OOM is
    // visible. On failure g_ui_ok stays false and ui_engine_frame() falls back
    // to the inline draw_ui().
    Serial.printf("[ui] pre-init  heap=%d psram=%d\n", ESP.getFreeHeap(), ESP.getFreePsram());
    g_ui_ok = walkie_ui::ui_engine_init(g_ui, &M5.Display);
    Serial.printf("[ui] post-init heap=%d psram=%d ok=%d\n", ESP.getFreeHeap(), ESP.getFreePsram(), (int)g_ui_ok);
#endif
    opus_stub_init();
    // Audio init must succeed before we connect; a "connected but silent" device
    // is worse than a visible failure. If the codec/I2S/task bring-up fails, mark
    // it so the UI can surface it (set via g_audio_ok; loop() shows a fault state).
    g_audio_ok = audio_hal_init();
    if (g_audio_ok) g_audio_ok = audio_task_init() && audio_task_start();
    if (!g_audio_ok) Serial.println("[audio] AUDIO BRING-UP FAILED — device will be silent");
    wifi_connect(WIFI_SSID, WIFI_PASS);
    state = WIFI_CONNECT;
    ws.begin(BACKEND_HOST, BACKEND_PORT, "/ws/cores3?device_token=" DEVICE_TOKEN);
    ws.onEvent(ws_handler);
    ws.setReconnectInterval(5000);
    Serial.println("setup complete");
}

// ==================================================================
// LOOP
// ==================================================================
void loop() {
    unsigned long now = millis();
    static unsigned long last_beat=0, last_keep=0, last_ui=0;

    M5.update();

    if(!WiFi.isConnected()) wifi_connect(WIFI_SSID, WIFI_PASS);
    ws.loop();
    if(state==WIFI_CONNECT&&WiFi.isConnected()) state=WSS_CONNECT;
    if((state==IDLE||state==WSS_CONNECT)&&now-last_keep>=30000){last_keep=now;send_keepalive();}

    // Drain encoded mic packets → server. Only while actively talking; bounded so
    // a backlog can't monopolise the loop. (Mic only produces frames in TALK.)
    if(amode==AUDIO_TALK){
        uint8_t pkt[AUDIO_MAX_OPUS_PACKET]; size_t plen; int budget=8;
        while(budget-- && audio_task_get_outbound_packet(pkt,&plen,0)) ws.sendBIN(pkt,plen);
    }

    // Touch
    auto touch = M5.Touch.getDetail();
    auto tp = M5.Touch.getTouchPointRaw(0);

    if(menu_open){
        if(touch.wasPressed()){
#ifdef UI_ENGINE
            // Use the lib's TESTED hit-test, which matches the lib menu renderer's
            // geometry (compute_menu_layout). The old tp.x/53 / tp.y/60 grid matched
            // NEITHER renderer → wrong/unreachable selection. Only select on a hit;
            // a miss still closes the menu (tap-outside-to-close).
            size_t hit = walkie_ui::menu_hit_test((int16_t)tp.x, (int16_t)tp.y, g_ui.menu_layout);
            if(hit < (size_t)NCHARS){ char_idx=(int)hit; send_switch(char_id((int)hit)); }
#else
            int col=tp.x/53,row=tp.y/60,idx=row*6+col;
            if(idx>=0&&idx<NCHARS){char_idx=idx;send_switch(char_id(idx));}
#endif
            menu_open=false; touch_pressed=false;
        }
    } else if(touch.wasPressed()&&state==IDLE&&!touch_pressed){
        touch_pressed=true;touch_down_at=now;send_session_start();
        Serial.println("→ SESSION_ACTIVE");
    }

    // Controls: HOLD = talk, quick TAP (<400ms) = open character menu.
    if(touch.wasReleased()&&touch_pressed){
        unsigned long held=now-touch_down_at;
        if(state==SESSION_ACTIVE){
            audio_set_mode(AUDIO_IDLE);send_session_end();state=IDLE;Serial.println("→ IDLE");
        } else if(held < 400){
            send_session_end(); menu_open=true; Serial.println("→ menu (tap)");
        } else {
            send_session_end(); Serial.println("→ IDLE (hold, no session)");
        }
        touch_pressed=false;
    }

    // Display throttle: 10 Hz idle, but throttled to 5 Hz while SESSION_ACTIVE so
    // the 150 KB sprite push / WS / Opus contend less for the loop during a live
    // turn. This is a safe, build-verifiable change; the actual on-device benefit
    // (out_drop staying 0 under load) MUST still be validated on hardware (soak).
    const unsigned long ui_interval = (state==SESSION_ACTIVE) ? 200 : 100;
    if(now-last_ui>=ui_interval){last_ui=now;
#ifdef UI_ENGINE
        ui_engine_frame();
#else
        draw_ui();
#endif
    }

    // Heartbeat
    if(now-last_beat>=10000){last_beat=now;
        Serial.printf("[idle] uptime=%ds wifi=%s rssi=%d heap=%d psram=%d state=%d amode=%d pcm_drop=%u out_drop=%u\n",(int)(now/1000),
            WiFi.isConnected()?WiFi.localIP().toString().c_str():"DOWN",WiFi.RSSI(),ESP.getFreeHeap(),ESP.getFreePsram(),state,amode,
            (unsigned)audio_task_pcm_drops(),(unsigned)audio_task_outbound_drops());}
}
