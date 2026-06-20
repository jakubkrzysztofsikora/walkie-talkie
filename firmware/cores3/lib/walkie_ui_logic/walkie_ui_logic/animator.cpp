#include "animator.h"

namespace walkie_ui {

AnimatorConfig default_animator_config() {
    return AnimatorConfig{
        .blink_interval_ticks = 7,
        .bounce_period_ticks = 12,
        .mouth_period_ticks = 4,
    };
}

Expression animator_tick(AnimatorState& s, MascotState desired, const AnimatorConfig& cfg) {
    s.tick++;

    // Map desired mascot state to expression.
    Expression target = Expression::IDLE;
    switch (desired) {
        case MascotState::IDLE: target = Expression::IDLE; break;
        case MascotState::LISTENING: target = Expression::LISTEN; break;
        case MascotState::THINKING: target = Expression::THINK; break;
        case MascotState::SPEAKING: target = Expression::SPEAK; break;
    }

    // Simple state transition without smoothing for v1.
    s.current_expression = target;

    // Blink: every blink_interval ticks, close eyes for 1 tick.
    s.eyes_open = (s.tick % cfg.blink_interval_ticks) != 0;

    // Bounce frame cycles through 0..bounce_period-1.
    s.bounce_frame = static_cast<uint8_t>(s.tick % cfg.bounce_period_ticks);

    // Mouth frame cycles quickly.
    s.mouth_frame = static_cast<uint8_t>(s.tick % cfg.mouth_period_ticks);

    return s.current_expression;
}

int8_t bounce_offset(const AnimatorState& s) {
    // Gentle bounce: -2..+2 px.
    if (s.bounce_frame < 3) return -static_cast<int8_t>(s.bounce_frame);
    if (s.bounce_frame < 6) return -static_cast<int8_t>(6 - s.bounce_frame);
    if (s.bounce_frame < 9) return static_cast<int8_t>(s.bounce_frame - 6);
    return static_cast<int8_t>(12 - s.bounce_frame);
}

MascotState screen_to_mascot_state(ScreenState screen, bool ptt_pressed, uint8_t speaking_level) {
    // Connect-time states show the THINKING (working) face.
    if (screen == ScreenState::WIFI_CONNECT || screen == ScreenState::WSS_CONNECT) {
        return MascotState::THINKING;
    }
    if (screen == ScreenState::SESSION_ACTIVE) {
        if (ptt_pressed) return MascotState::LISTENING;
        if (speaking_level > 0) return MascotState::SPEAKING;
        return MascotState::THINKING;
    }
    return MascotState::IDLE;
}

Expression compute_screen(AudioMode amode, ScreenState state, bool ptt_pressed, bool menu_open) {
    (void)ptt_pressed;  // PTT intent is reflected via SESSION_ACTIVE + AUDIO_TALK.
    if (menu_open) return Expression::IDLE;

    // Agent reply is playing: SPEAK regardless of DeviceState. This MUST win over
    // the connect/session checks so the post-release reply (which arrives in IDLE)
    // still shows the speaking face.
    if (amode == AudioMode::AUDIO_LISTEN) return Expression::SPEAK;

    if (state == ScreenState::WIFI_CONNECT || state == ScreenState::WSS_CONNECT) {
        return Expression::THINK;
    }

    // User is holding PTT / talking during a live session.
    if (state == ScreenState::SESSION_ACTIVE && amode == AudioMode::AUDIO_TALK) {
        return Expression::LISTEN;
    }

    return Expression::IDLE;
}

} // namespace walkie_ui
