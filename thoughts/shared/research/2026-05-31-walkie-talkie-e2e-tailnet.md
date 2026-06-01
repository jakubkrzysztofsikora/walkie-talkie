---
date: 2026-05-31
commit: n/a (no git)
branch: n/a
tags: [elevenlabs, tailnet, polish, e2e, audio-pipeline, messaging, spec-compliance]
status: complete
---
# Research: Walkie-Talkie E2E on Tailnet — Full Codebase Audit

## Summary

The walkie-talkie repo is a Python 3.11 FastAPI/WebSocket voice agent that bridges
hardware walkie-talkie clients to ElevenLabs Conversational AI. The codebase is
~80% spec-compliant, fully wired for ElevenLabs, and structurally ready to run on
Tailnet. There are three bugs and several gaps blocking a clean E2E test today:
a double-resampling bug in the audio pipeline, missing Polish personalities, and
an unused `AuditLogger` class (ConfigDatabase handles all DB writes in practice).

---

## Files Involved

### Core Application
| File | Layer | Purpose |
|------|-------|---------|
| `walkie_agent/main.py` | Entry | Routes to CLI or uvicorn server |
| `walkie_agent/api.py` | API | FastAPI app, WebSocket endpoint, REST admin endpoints |
| `walkie_agent/settings.py` | Config | Pydantic settings with `WALKIE_` prefix env vars |

### Audio Pipeline
| File | Layer | Purpose |
|------|-------|---------|
| `walkie_agent/audio/websocket.py` | Pipeline | Per-client lifecycle: accept WS, load personality, VAD buffer, ElevenLabs bridge |
| `walkie_agent/audio/processor.py` | Pipeline | `AudioBuffer` (WebRTC VAD-based utterance segmentation) + `AudioFormatConverter` (μ-law/PCM/resample) |
| `walkie_agent/audio/elevenlabs_bridge.py` | Bridge | Bidirectional ElevenLabs ConvAI WebSocket: audio send/receive, ping/pong, tool calls, transcripts |

### Agent / AI
| File | Layer | Purpose |
|------|-------|---------|
| `walkie_agent/agent/engine.py` | Engine | State machine: IDLE→LISTENING→THINKING→SPEAKING→TOOL_CALL→ENDED + safety filter |
| `walkie_agent/agent/personality.py` | Config | `PersonalityConfig` dataclass + `PersonalityManager` (separate SQLite) + `build_system_prompt()` |
| `walkie_agent/agent/kid_friendly.py` | Safety | `KidSafetyFilter`: blocked keyword list, topic classification, vocabulary simplification, warmth injection |
| `walkie_agent/agent/tool_router.py` | Routing | `BaseTool` ABC + `ToolRegistry` with OpenAI schema generation |

### Tools
| File | Layer | Purpose |
|------|-------|---------|
| `walkie_agent/tools/search.py` | Tool | `WebSearchTool` — DuckDuckGo via `duckduckgo-search` |
| `walkie_agent/tools/sandbox.py` | Tool | `SandboxTool` — AST-checked Python execution with import whitelist |
| `walkie_agent/tools/messaging.py` | Tool | `MessagingTool` — WhatsApp (Twilio), Telegram (Bot API), Signal (signal-cli) + `WhitelistManager` |
| `walkie_agent/tools/base.py` | Tool | `BaseTool` abstract base (duplicated in tool_router.py — minor redundancy) |

### Persistence
| File | Layer | Purpose |
|------|-------|---------|
| `walkie_agent/config/database.py` | DB | `ConfigDatabase` — unified async SQLite for personalities, whitelist, conversations, transcripts, tool_calls |
| `walkie_agent/config/models.py` | Models | Pydantic models for API request/response |
| `walkie_agent/config/cli.py` | Admin | Typer CLI: `agent`, `whitelist`, `logs` sub-commands |
| `walkie_agent/audit/logger.py` | DB | `AuditDatabase` + `AuditLogger` — *separate, currently unused* (api.py uses ConfigDatabase instead) |
| `walkie_agent/audit/models.py` | Models | `ConversationRecord`, `TranscriptEntry`, `ToolCallRecord`, `ConversationStats` |

---

## Data Flow

### Inbound Audio (client → ElevenLabs)
```
Client WebSocket (binary μ-law 8kHz)
  → api.py:walkie_websocket() [line 271]
  → AudioWebSocketManager.handle_client() [websocket.py:133]
  → _client_to_elevenlabs_loop() [websocket.py:296]
      - AudioFormatConverter.ulaw_to_pcm(chunk)   [processor.py:52]
      - AudioFormatConverter.resample(pcm, 8000→16000) [processor.py:143]
      - AudioBuffer.add_chunk(pcm_16k)             [processor.py:312]
          → WebRTC VAD frames utterance
          → returns bytes when speech ends
      - elevenlabs_bridge.send_audio(utterance)    [elevenlabs_bridge.py:339]
          ⚠️ BUG: bridge receives 16kHz PCM but calls _resample_linear(pcm, 8000→16000) again
          - base64-encodes PCM 16kHz → sends user_audio_chunk JSON
```

### Outbound Audio (ElevenLabs → client)
```
ElevenLabs ConvAI WebSocket (PCM 16kHz, base64)
  → _receive_loop() [elevenlabs_bridge.py:455]
  → _handle_audio() [elevenlabs_bridge.py:554]
      - base64-decode PCM 16kHz
      - _resample_linear(pcm_16k, 16000→8000)
      - _pcm_to_ulaw(pcm_8k)
      → asyncio.Queue (audio_queue)
  → receive_audio() generator [elevenlabs_bridge.py:395]
  → _elevenlabs_to_client_loop() [websocket.py:437]
  → websocket.send_bytes(ulaw_8k)  [websocket.py:464]
```

### Tool Call Flow
```
ElevenLabs sends tool_call JSON
  → _handle_tool_call() [elevenlabs_bridge.py:735]
      - calls on_tool_call(tool_name, params) callback
  → _make_tool_call_callback() closure [websocket.py:681]
      - _ToolRegistryProxy.execute(client_id, tool_name, params) [api.py:255]
      - ToolRegistry.get_tool(tool_name) + tool.execute(**params) [tool_router.py:191]
  → sends tool_result JSON back to ElevenLabs [elevenlabs_bridge.py:790]
```

### Personality Loading (api.py runtime)
```
api.py:walkie_websocket() 
  → _PersonalityManagerProxy.load_personality(client_id) [api.py:237]
  → ConfigDatabase.get_default_personality()
  → builds "prompt" = f"{backstory}\n{custom_instructions}"  ← ⚠️ NOT using build_system_prompt()
```

---

## Existing Patterns

### ElevenLabs ConvAI WebSocket Message Types (elevenlabs_bridge.py)
The bridge handles these incoming message types from ElevenLabs:
- `audio` → decodes + queues PCM to send back to client
- `user_transcript` → calls on_transcript("user", text)
- `agent_response` → calls on_transcript("agent", text)
- `interruption` → drains the audio queue
- `ping` → sends pong with ping_id
- `error` → logs; disconnects on auth_error/rate_limit/invalid_request
- `tool_call` → routes to on_tool_call callback, sends tool_result
- `conversation_initiation_metadata` → logged at DEBUG

Connection initiation sends `conversation_initiation_client_data` with:
```json
{
  "type": "conversation_initiation_client_data",
  "conversation_config_override": {
    "agent": {
      "prompt": {"prompt": "<system_prompt>"},
      "voice_settings": {<optional>}
    }
  }
}
```

### Seeded Default Personality (config/database.py:603)
`seed_default_personality()` inserts "Captain Sparkles" (English) if no personalities exist.
Voice ID: `XB0fDUnXU5powFXDhCwa` (English voice).

---

## Architecture Notes

### Dual Database Problem
`config/database.py:ConfigDatabase` and `audit/logger.py:AuditDatabase` both define
identical schemas (conversations, transcripts, tool_calls, system_events). `api.py`
uses only `ConfigDatabase`. The entire `audit/` module is not imported or called from `api.py`.
This means `AuditLogger` / `AuditDatabase` are dead code for the running server.

### Tool Registry Wrapping
`api.py` wraps `ToolRegistry` in `_ToolRegistryProxy` [api.py:249] which calls
`tool.execute(**params)` directly, bypassing `AgentEngine.handle_tool_call()` and
its permission check (allowed_tools list). Tools work but personality-level tool
whitelisting is NOT enforced at runtime.

### AudioWebSocketManager and Settings
`AudioWebSocketManager.__init__` accesses settings via `getattr(self.settings, ...)` with
fallbacks — it does NOT require a specific class, just duck-typed attributes. The real
`Settings` Pydantic class satisfies all these accesses.

### Missing `vad_silence_duration_ms` in Settings
`websocket.py:185` does `getattr(self.settings, "vad_silence_duration_ms", 1000)` but
`settings.py` does NOT declare `vad_silence_duration_ms`. This silently uses the default
1000ms — fine for now but invisible.

---

## External Dependencies

| Service | Config | Status |
|---------|--------|--------|
| ElevenLabs ConvAI | `WALKIE_ELEVENLABS_API_KEY` + `WALKIE_ELEVENLABS_AGENT_ID` | Required; must have an ElevenLabs Agent configured in the dashboard |
| OpenAI | `WALKIE_OPENAI_API_KEY` | Listed in settings but unused in code (ElevenLabs handles LLM) |
| Twilio (WhatsApp) | `WALKIE_TWILIO_ACCOUNT_SID/AUTH_TOKEN/PHONE_NUMBER` | Optional; needs WhatsApp-enabled Twilio number |
| Telegram | `WALKIE_TELEGRAM_BOT_TOKEN` | Optional |
| Signal | `WALKIE_SIGNAL_PHONE_NUMBER` + `signal-cli` in PATH | Optional |
| DuckDuckGo | None | No API key; `duckduckgo-search` package |
| webrtcvad | None | Python package; needs `webrtcvad-wheels` for macOS |
| NumPy | None | Optional; falls back to `audioop.ratecv` |

---

## Bugs & Gaps

### BUG 1: Double Resampling in Audio Pipeline (BLOCKING)
**Location**: `websocket.py:374-378` + `elevenlabs_bridge.py:360-367`

`websocket.py` resamples the incoming walkie audio from 8kHz→16kHz BEFORE feeding it
to `AudioBuffer` for VAD. The VAD returns utterances already at 16kHz PCM.
When those utterances are passed to `elevenlabs_bridge.send_audio()`, the bridge
`self.audio_format = "ulaw"` and `self.sample_rate = 8000` cause it to call
`_resample_linear(pcm, 8000, 16000)` again on already-16kHz data, resulting in
double the expected audio length and corrupted pitch sent to ElevenLabs.

**Fix**: Either (a) pass the original walkie-format audio to `send_audio()` and let the
bridge handle full conversion, or (b) change `send_audio()` to accept pre-converted
16kHz PCM with a flag.

### BUG 2: Typo in Safety Guardrails (MINOR)
**Location**: `agent/personality.py:487`
```python
"ALREADY respond in the language specified by the system."
# should be:
"ALWAYS respond in the language specified by the system."
```

### BUG 3: `process_agent_response` Typo (MINOR)
**Location**: `agent/engine.py:284`
```python
self.audit_log.info(...)  # should be self.audit_logger.info(...)
```
`audit_log` is a `List[_AuditEntry]`, not a Logger. This line raises `AttributeError`
if agent output is ever scrubbed (rare path, but it will crash when hit).

### GAP 1: Polish Language Support
- No Polish personality seeded
- `build_system_prompt()` puts "ALWAYS respond in the language specified by the system" but
  never actually specifies the language in the prompt — LLM must infer from `language` field
  which is never injected into the actual prompt text
- `WALKIE_ELEVENLABS_AGENT_ID` points to a single ElevenLabs agent — Polish responses
  require an ElevenLabs agent configured with a Polish TTS voice

### GAP 2: Empty Tests
`tests/conftest.py` and `tests/__init__.py` are both empty (0 bytes).
No test files exist (`test_audio.py`, `test_agent.py`, etc. referenced in SPEC do not exist).

### GAP 3: Personality Prompt Builder Not Used at Runtime
`PersonalityManager.build_system_prompt()` (personality.py:416) builds a rich structured
prompt with all traits, kid-safety rules, and language instructions. But `api.py:245`
only uses: `f"{backstory}\n{custom_instructions}"`. The elaborate prompt template is
built but never applied in the running server.

### GAP 4: No `WALKIE_ELEVENLABS_AGENT_ID` in .env.example
The `.env.example` file could not be read (permission denied), but `settings.py` shows
`elevenlabs_agent_id: str = ""` — connecting to ElevenLabs ConvAI requires a valid agent
ID configured in the ElevenLabs dashboard. Without it, the WebSocket connect will fail.

---

## Tailnet Deployment Notes

Running on Tailnet requires no code changes. The server already binds to `0.0.0.0:8000`.
Steps:
1. Run server on any Tailnet-enrolled machine
2. Other Tailnet machines connect to `ws://<tailscale-ip>:8000/ws/walkie-talkie/<client_id>`
3. `docker-compose.yml` maps `8000:8000` and supports this out of the box

For production: consider `WALKIE_HOST=0.0.0.0` (already default) and optionally
set `--serve-funnel` in Tailscale for external access.

---

## Open Questions

1. **ElevenLabs Agent ID**: Does a ConvAI agent already exist in the ElevenLabs dashboard?
   The API key is available but `elevenlabs_agent_id` must be set. This is blocking.

2. **Walkie-talkie hardware**: When the spec says "connect to walkie-talkie ranges" — is this
   the actual BAOFENG radio + Raspberry Pi setup, or testing via a browser/script that connects
   to the WebSocket endpoint with PCM/μ-law audio?

3. **Polish language scope**: Should Polish be a new personality (language="pl", Polish backstory,
   Polish ElevenLabs voice ID) or should the entire system switch to Polish?

4. **Which machine runs the server on Tailnet**: Dev Mac, or a separate host?

5. **Messaging for E2E test**: Is Telegram or WhatsApp configured? For E2E testing of the
   `send_message` tool, a whitelisted contact + Twilio or Telegram bot token is needed.
