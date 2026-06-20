#pragma once
/**
 * Animation timing engine for the mascot.
 *
 * Drives blink, bounce, mouth flap and expression transitions.
 * Designed to be deterministic and unit-testable.
 */

#ifdef ARDUINO
#include <Arduino.h>
#else
#include <cstdint>
#endif
#include "sprites.h"

namespace walkie_ui {

// Mirrors the device state machine in main.cpp.
enum class ScreenState : uint8_t {
    BOOT,
    WIFI_CONNECT,
    WSS_CONNECT,
    IDLE,
    SESSION_ACTIVE
};

enum class MascotState : uint8_t {
    IDLE,
    LISTENING,
    THINKING,
    SPEAKING
};

// Mirrors the half-duplex audio mode enum in main.cpp.
// AUDIO_TALK  = mic hot, user holding PTT.
// AUDIO_LISTEN = agent TTS playing (mic forced off). This happens both during a
//   SESSION_ACTIVE turn AND in IDLE (the post-release reply), so the mapping must
//   not gate the speaking face on DeviceState.
enum class AudioMode : uint8_t {
    AUDIO_IDLE,
    AUDIO_TALK,
    AUDIO_LISTEN
};

struct AnimatorConfig {
    uint8_t blink_interval_ticks;   // how many ticks between blinks
    uint8_t bounce_period_ticks;    // bounce loop length
    uint8_t mouth_period_ticks;     // mouth flap loop length
};

struct AnimatorState {
    uint32_t tick = 0;
    MascotState state = MascotState::IDLE;
    Expression current_expression = Expression::IDLE;
    bool eyes_open = true;
    uint8_t bounce_frame = 0;
    uint8_t mouth_frame = 0;
};

// Default config tuned for 10 ticks/sec (100 ms/tick).
AnimatorConfig default_animator_config();

// Advance the animator by one tick. Returns the expression to render.
Expression animator_tick(AnimatorState& s, MascotState desired, const AnimatorConfig& cfg);

// How many pixels to offset the mascot vertically for the current bounce.
int8_t bounce_offset(const AnimatorState& s);

// Convert the device state to the animator state used for mascot expression.
MascotState screen_to_mascot_state(ScreenState screen, bool ptt_pressed, uint8_t speaking_level);

// Pure decision function: map the full (audio mode + device state) pair to the
// expression to render. Keyed primarily on `amode` so the agent-speaking face
// (SPEAK) shows whenever TTS is playing, regardless of DeviceState — including
// the post-PTT-release reply that streams while the device is back in IDLE.
//
// Mapping (first match wins):
//   menu_open                                    -> IDLE   (menu draws its own UI)
//   amode == AUDIO_LISTEN  (agent reply, any state) -> SPEAK
//   WIFI_CONNECT or WSS_CONNECT                   -> THINK
//   SESSION_ACTIVE && amode == AUDIO_TALK         -> LISTEN (user holding/talking)
//   otherwise                                     -> IDLE
Expression compute_screen(AudioMode amode, ScreenState state, bool ptt_pressed, bool menu_open);

} // namespace walkie_ui
