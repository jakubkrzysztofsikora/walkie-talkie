# CoreS3 SPEC ↔ PLAN review (2026-06-02)

## Verdict

Ship with minor edits. The PLAN covers all SPEC sections in execution-relevant scope and the dependency graph is acyclic. Several SPEC sub-concerns (notably the 7 reported gaps) are addressed implicitly inside task notes rather than surfaced in a single visible risk-register entry — this is a documentation hygiene issue, not a blocker.

## Coverage matrix

| SPEC § | Title | Covered by | Notes |
|--------|-------|------------|-------|
| §1 | Purpose & Non-Goals | P0 (reading), P16 (v2 deferral) | Non-goals (wake-word, battery, LCD UI) are explicitly deferred to P16. |
| §2 | Hardware & Wiring | P0 (`esphome-m5cores3.yaml` GPIO ref), P10 (HAL bring-up) | GPIO table consumed by P10's `audio_hal.cpp`. |
| §3 | Architecture Diagram | P1, P5, P12 | Bridge components from §3 map 1:1 to P1+P5; full chain proven in P12. |
| §4 | Audio Chain Spec | P2 (server), P9 (firmware stub), P11 (loopback) | Sample-rate/bitrate constants explicitly locked in `opus_codec.py` and `opus_stub.cpp`. Partial-frame handling is mentioned in P3 acceptance (reject + log) — see Issues. |
| §5 | Wire Protocol v1 | P1, P3, P6, P8 | All control message types covered. `agent_interrupted` text frame referenced in P3 but missing from §5 enum — see Issues. |
| §6.1 | `cores3_bridge.py` / `CoreS3AudioInterface` | P1 (skeleton), P3 (real impl) | Encoder/decoder ownership clarified in P3 (decoder lives in session handler, encoder in interface). |
| §6.2 | `WebSocket /ws/cores3` | P1 | Endpoint registered in `api.py` per spec; token validation hook landed in P4. |
| §6.3 | `opus_codec.py` | P2 | Constants and stateful encoder/decoder per session. |
| §6.4 | `cores3_token.py` | P4 | Schema, CLI, lookup/issue/revoke all present. |
| §6.5 | Tool Routing | P5, P14 | 11 tools enumerated in P5; `ask_expert` appears in §6.5 table — see Issues for switch_character enum gap. |
| §7 | Firmware Spec | P7, P8, P9, P10, P11, P13, P15 | Build system in P7; task architecture in P8/P10/P11; state machine in P8; TLS in P8. |
| §8 | Memory Budget | P9 (PSRAM allocation comments), P11 (heap measurement), P15 (heap monitor) | Verified by `heap_caps_get_free_size` in P11 acceptance. |
| §9 | Power & Battery | P16 (deferred) | Explicitly deferred per spec. |
| §10 | Wake-Word Options | P16 (deferred) | All three options carried forward as v2 backlog. |
| §11 | Existing References | P0, X1 | ElatoAI + ESPHome configs cloned into `firmware/cores3/references/`. |
| §12 | Risks & Open Questions | X2 (risk register), P10/P11/P13 (closes RQs) | RQ→phase mapping documented in X2 acceptance criteria. |
| §13 | Test Plan (Pre-Device) | P2 (T2), P6 (T3), P5 (T4), P7 (T5) | T1 (real-SDK smoke) is not explicitly listed — see Issues. T6 (ElatoAI spare-devkit smoke) not in plan. |
| §14 | Glossary | — | Reference only, no task needed. |
| §15 | Sources | P0 (`READING_LIST.md`) | Source numbers enumerated in P0 acceptance. |

## 7 spec-gap follow-ups

| Gap | Where addressed in PLAN | Notes |
|-----|-------------------------|-------|
| Encoder ownership ambiguity in `CoreS3AudioInterface` (spec §6.1) | P3 body — explicit clarification: interface owns encoder, session handler owns decoder | Addressed inline in P3 task text, not in X2 risk register. Visible enough. |
| Partial-frame handling in `output()` (spec §4) | P3 acceptance criterion 3 — "final frame zero-pads or rejects partial — choose reject + log" | Decision is left to implementer at code time; not pre-decided. Minor risk of inconsistency vs Opus framing. |
| `agent_interrupted` text frame not in §5 control messages | P3 body specifies the frame; not added to §5 schema list | Plan introduces the message but does not flag the spec-§5 omission. Should be noted in X2 or as a "spec patch needed" item. |
| Sentinel byte `0xFF` collision risk | P3 body specifies sentinel as `b"\xff"`; no analysis of collision risk against valid Opus packet starts | Not addressed. Opus packets can begin with 0xFF in TOC byte combinations — see Issues. |
| `ask_expert` listed in §6.5 but not in switch_character enum | P5 enumerates `ask_expert` as the 11th tool; spec §5 character enum is unrelated (different concept — characters vs tools) | The reported "gap" appears to conflate two separate enumerations. `ask_expert` is a tool, not a character. P5 correctly includes it. Possibly a misreport by the previous agent. |
| CLI invocation pattern assumes `walkie_agent/main.py` | P4 body: "Touch walkie_agent/main.py" with alternative `python -m walkie_agent` | Plan provides both invocation forms; assumption is hedged. |
| `pio test -e native` feasibility for arduino-libopus | P9 acceptance criterion 3 — "`pio test -e native` either passes (preferred) or is documented as deferred to P10" | Explicitly acknowledged as uncertain; fallback plan provided. |

## Issues found

- **[major]** No consolidated risk register entry for the 7 spec gaps. X2 copies §12 verbatim but the 7 follow-ups are scattered across task bodies. Fix: append a "Spec gaps from planning audit" section to X2.
- **[major]** Sentinel byte `0xFF` collision: Opus packet TOC bytes can be 0xFF (config 31, stereo, code 3) so the device cannot reliably distinguish a flush sentinel from a valid 1-byte malformed packet. Fix: use a 2+ byte sentinel or a text-frame-only `agent_interrupted` signal (drop the binary sentinel).
- **[major]** `agent_interrupted` text frame missing from spec §5 control message enum. Fix: either add it to §5 in a spec-patch task, or remove the binary sentinel and rely solely on a §5-listed text frame.
- **[minor]** SPEC §13 T1 (real ElevenLabs SDK smoke test with valid API key) has no corresponding PLAN task. P3 uses a fake `Conversation`; P6 patches the SDK; no phase actually instantiates the live SDK pre-device. Fix: add a P3.5 or extend P6 acceptance with a `@pytest.mark.live` smoke test.
- **[minor]** SPEC §13 T6 (ElatoAI smoke on spare ESP32-S3 devkit) is not in the plan. Acceptable to drop since the test is "if available," but should be explicitly marked NOT-PLANNED in P0 or X2.
- **[minor]** P3 acceptance leaves "zero-pad vs reject" partial-frame decision to implementer. Fix: pre-decide in plan (recommend zero-pad with comment, since dropping causes audible click at TTS boundaries).
- **[minor]** P5 says "ask the existing handler functions are exposed importably (factor out the body of each `@app.post(...)` handler)." This is a non-trivial refactor of the existing `api.py` touched in the same task. Fix: split into P5a (refactor handlers) and P5b (wire into bridge).
- **[minor]** Effort summary lists `P0:0.5, P1:1.5, ... X3:0.25` totaling 14 days, but adding the listed values: 0.5+1.5+0.5+1.5+1+2+1.5+0.5+1.5+1.5+0.5+0.5+0.25 = 13.25 days. The "~14" rounding is generous but acceptable.
- **[minor]** P16 listed as "4 days, deferred" but is included in the 15-day post-device total. If truly deferred from v1 critical path, it should be excluded from the post-device total (which then becomes ~11 days).
- **[minor]** RQ-3 (AEC) reduction is mapped to P13 in X2 acceptance ("RQ-3 reduced at P13"), but P13's body is touch PTT + LED — no AEC mitigation work is actually scheduled. The PTT model implicitly reduces echo, but this should be called out explicitly.

## Effort sanity check

- Pre-device: ~14 dev-days (13.25 by sum). Distribution looks balanced — bridge work (P1+P3+P5 = 5 days) is the biggest chunk, which matches the architectural risk. Firmware skeleton phases (P7+P8+P9 = 3.5 days) are tight but defensible for compile-only work.
- Post-device: ~15 dev-days including 4-day P16 backlog item. Excluding P16, the actual v1 post-device work is ~11 days. Soak test (P15: 2 days) and tool validation (P14: 2 days) are appropriately sized.
- Split looks correct: pre-device is heavier on Python (where it can be tested), post-device is heavier on integration (where the hardware is needed). No phase looks suspiciously light.

## Dependency graph audit

Graph is **acyclic**. Verified by traversal:

- Root nodes (no deps): P0
- All other nodes trace back to P0 through 1+ paths.
- P11 has deps on P9, P10, P3 — note that P3 is pre-device and is reused here; this is correct (server-side encoder params must match).
- P12 depends on P11 and P6 — cross-cuts pre and post device, but only after MILESTONE, so valid.
- MILESTONE gate correctly depends on P6, P9, X1 (all pre-device terminal nodes for their lanes).
- X3 has no downstream consumers — flagged as isolated but acceptable since it's a memory-only side-effect task.
- No task before MILESTONE requires the device. Verified by scanning P0–P9 + X1–X3 acceptance criteria — all are compile-only, doc-only, or unit-test-only.
- Minor observation: X1 depends on P0, P4, P7 but is in the MILESTONE gate's prerequisite set. This is consistent because X1 documents the provisioning workflow which is needed before the operator can flash a device.

No dangling deps. No cycles. No phase claims a device-only resource before MILESTONE.
