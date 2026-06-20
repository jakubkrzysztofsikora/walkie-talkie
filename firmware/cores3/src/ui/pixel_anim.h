#pragma once
/**
 * Device-side PXA1 pixel-animation overlay (CoreS3).
 *
 * Receives a single PXA1 binary WS message, copies its frames into a PSRAM
 * buffer (the WS payload is freed after the handler returns), and composites
 * the current frame as an overlay ON TOP of the normal mascot back-buffer.
 * Auto-dismisses after a fixed duration.
 *
 * Pure parse/validate/timing math lives in the host-testable lib
 * (walkie_ui_logic/pixel_anim.h). This module owns only the PSRAM alloc and the
 * masked RGB565 blit (same run-blit technique as ui_engine's draw_sprite_native).
 *
 * Guarded so the non-UI_ENGINE build still compiles: the blit entry point takes
 * the UIEngine only under UI_ENGINE; pixel_anim_set/active are always available.
 */

#include <Arduino.h>
#include <cstdint>
#include <cstddef>

#include "walkie_ui_logic/pixel_anim.h"  // PxaHeader, PXA_MAX_DIM

#ifdef UI_ENGINE
#include "ui_engine.h"
#endif

// Upscale factor for the overlay blit. 48x48 is tiny on a 320x240 screen; scale
// 3 -> 144x144 centred. Kept modest so the masked-run pushImage stays cheap.
// Lives in the header (not the .cpp) so PXA_MAX_RUN_PX and the static_assert
// below can be expressed in terms of it.
static constexpr int16_t PXA_SCALE = 3;

// Max scaled-run buffer width: a source run is at most PXA_MAX_DIM (64) px, and
// the overlay upscales by PXA_SCALE (3), so a single run is <= 192 scaled px.
static constexpr int PXA_MAX_RUN_PX = (int)walkie_ui::PXA_MAX_DIM * PXA_SCALE;

// Bumping PXA_SCALE must never let a full scaled row overflow the static run[]
// buffer (sized to PXA_MAX_RUN_PX). Fail the build instead of silently writing OOB.
static_assert(PXA_MAX_RUN_PX >= (int)walkie_ui::PXA_MAX_DIM * PXA_SCALE,
              "run buffer must hold a full scaled row");

// Store the PXA1 payload into a fresh PSRAM frame buffer. Validates via the
// pure lib parser; returns false (and changes nothing) on a malformed message.
// Replaces any currently-playing animation (frees the old buffer first).
bool pixel_anim_set(const uint8_t* payload, size_t len);

// True while an animation is loaded AND within its play duration. Flips to
// false (and frees the PSRAM buffer) once the duration elapses.
bool pixel_anim_active();

// Free any PSRAM buffer and clear state (idempotent).
void pixel_anim_clear();

#ifdef UI_ENGINE
// Advance the current frame on its frame_ms cadence and blit it centred,
// scaled-up, into ui.back_buffer as an overlay. No-op if inactive or the
// back-buffer is unusable. Auto-dismisses when the duration elapses.
void pixel_anim_tick_and_draw(walkie_ui::UIEngine& ui);
#endif
