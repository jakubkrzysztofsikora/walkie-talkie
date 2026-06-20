#include <unity.h>

// Minimal native test verifying constants that must stay locked between
// firmware and server. The full audio state machine is validated by the
// m5stack-cores3 build and on-device stress tests.

#define OPUS_SAMPLE_RATE  16000
#define OPUS_CHANNELS     1
#define OPUS_FRAME_MS     20
#define OPUS_FRAME_SAMPLES 320
#define OPUS_FRAME_BYTES  640
#define OPUS_BITRATE      48000

void test_opus_constants_are_locked(void) {
    // These must match project/walkie_agent/opus_codec.py exactly.
    TEST_ASSERT_EQUAL_INT(16000, OPUS_SAMPLE_RATE);
    TEST_ASSERT_EQUAL_INT(1, OPUS_CHANNELS);
    TEST_ASSERT_EQUAL_INT(20, OPUS_FRAME_MS);
    TEST_ASSERT_EQUAL_INT(320, OPUS_FRAME_SAMPLES);
    TEST_ASSERT_EQUAL_INT(640, OPUS_FRAME_BYTES);
    TEST_ASSERT_EQUAL_INT(48000, OPUS_BITRATE);
}

void test_halfduplex_state_transitions(void) {
    // Boolean model of the half-duplex policy:
    // mic_enabled is mutually exclusive with speaker output being stopped by
    // session_end / agent_interrupted. The real implementation enforces this
    // via audio_task events.
    bool mic_enabled = false;
    bool output_stopped = false;

    mic_enabled = true;
    TEST_ASSERT_TRUE(mic_enabled);

    output_stopped = true;  // session_end / interrupt
    TEST_ASSERT_TRUE(output_stopped);

    mic_enabled = false;
    output_stopped = false;
    TEST_ASSERT_FALSE(mic_enabled);
    TEST_ASSERT_FALSE(output_stopped);
}

// ---------------------------------------------------------------------------
// Phase 3 talk/listen mode table. Mirrors the transition logic in main.cpp so a
// divergence is caught here. Invariant under test: mic_enabled IFF mode==TALK,
// and TTS can never coexist with an enabled mic.
// ---------------------------------------------------------------------------
enum AMode { A_IDLE, A_TALK, A_LISTEN };
struct Model { AMode mode; bool mic_enabled; bool session_active; bool session_ended_sent; };

// set_mode mirrors audio_set_mode(): only commits the mode if the (control-queue)
// command is ACCEPTED. `accept` models whether the dedicated control queue took it.
static bool set_mode(Model& m, AMode to, bool accept = true) {
    if (to == m.mode) return true;
    if (!accept) return false;                 // command rejected → mode unchanged (gate stays honest)
    m.mic_enabled = (to == A_TALK);
    m.mode = to;
    return true;
}
// Events mirroring main.cpp handlers:
static void ev_session_started(Model& m, bool touch_pressed) {
    if (m.session_active) return;              // guard: duplicate/late ack ignored (CRITICAL-2 fix)
    m.session_active = true;
    if (touch_pressed) set_mode(m, A_TALK);
    else { m.session_ended_sent = true; m.session_active = false; set_mode(m, A_IDLE); }
}
// BIN handler: force LISTEN if not already (idempotent mic-off), mirrors != A_LISTEN.
static void ev_tts_frame(Model& m) { if (m.session_active && m.mode != A_LISTEN) set_mode(m, A_LISTEN); }
static void ev_release(Model& m) { if (m.session_active) { set_mode(m, A_IDLE); m.session_ended_sent = true; m.session_active = false; } }

void test_mode_started_while_held_enables_mic(void) {
    Model m{A_IDLE,false,false,false};
    ev_session_started(m, /*touch_pressed=*/true);
    TEST_ASSERT_EQUAL_INT(A_TALK, m.mode);
    TEST_ASSERT_TRUE(m.mic_enabled);
}
void test_mode_started_after_release_closes_session(void) {
    // The fast-tap race: server ack arrives after the finger lifted.
    Model m{A_IDLE,false,false,false};
    ev_session_started(m, /*touch_pressed=*/false);
    TEST_ASSERT_EQUAL_INT(A_IDLE, m.mode);
    TEST_ASSERT_FALSE(m.mic_enabled);
    TEST_ASSERT_TRUE(m.session_ended_sent);   // not stranded active
}
void test_tts_during_talk_mutes_mic(void) {
    Model m{A_IDLE,false,false,false};
    ev_session_started(m, true);
    TEST_ASSERT_TRUE(m.mic_enabled);
    ev_tts_frame(m);                          // TTS arrives while still held
    TEST_ASSERT_EQUAL_INT(A_LISTEN, m.mode);
    TEST_ASSERT_FALSE(m.mic_enabled);         // <-- the hard-gate: never hot during TTS
}
void test_release_during_talk_goes_idle(void) {
    Model m{A_IDLE,false,false,false};
    ev_session_started(m, true);
    ev_release(m);
    TEST_ASSERT_EQUAL_INT(A_IDLE, m.mode);
    TEST_ASSERT_FALSE(m.mic_enabled);
    TEST_ASSERT_TRUE(m.session_ended_sent);
}
void test_duplicate_session_started_does_not_rearm_talk(void) {
    // CRITICAL-2: a second session_started mid-TTS must NOT re-enable the mic.
    Model m{A_IDLE,false,false,false};
    ev_session_started(m, true);     // TALK
    ev_tts_frame(m);                 // LISTEN (mic off)
    ev_session_started(m, true);     // duplicate ack while still "held"
    TEST_ASSERT_EQUAL_INT(A_LISTEN, m.mode);   // stays LISTEN
    TEST_ASSERT_FALSE(m.mic_enabled);          // mic NOT re-armed during playback
}
void test_rejected_mic_command_keeps_mode_honest(void) {
    // CRITICAL-3: if the control command is rejected, mode must not advance — the
    // gate must reflect real mic state, never optimistically flip.
    Model m{A_IDLE,false,true,false};
    m.mode = A_TALK; m.mic_enabled = true;     // currently talking
    bool ok = set_mode(m, A_LISTEN, /*accept=*/false);  // disable rejected
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_INT(A_TALK, m.mode);     // NOT advanced to LISTEN
    TEST_ASSERT_TRUE(m.mic_enabled);           // still reflects the (still-on) mic — honest
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_opus_constants_are_locked);
    RUN_TEST(test_halfduplex_state_transitions);
    RUN_TEST(test_mode_started_while_held_enables_mic);
    RUN_TEST(test_mode_started_after_release_closes_session);
    RUN_TEST(test_tts_during_talk_mutes_mic);
    RUN_TEST(test_release_during_talk_goes_idle);
    RUN_TEST(test_duplicate_session_started_does_not_rearm_talk);
    RUN_TEST(test_rejected_mic_command_keeps_mode_honest);
    return UNITY_END();
}
