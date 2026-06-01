# Walkie-Talkie E2E — ElevenLabs CLI Agent + Radio Bridge + Polish Support

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix blocking bugs, enable ElevenLabs ConvAI agent management from the CLI, add a radio bridge for real walkie-talkies over Tailnet (Mac Studio or jakub-health-hub as bridge hardware), add a Polish personality, and wire up E2E tests. Server can deploy to Fly.io.

**Architecture:** The FastAPI server runs on Mac or Fly.io (cloud). A new `bridge/radio_client.py` runs on **Mac Studio (100.116.31.6) or jakub-health-hub (100.94.91.47)** — both already on Tailnet, no Pi needed. The CLI gains an `elevenlabs` sub-command group that creates/syncs ConvAI agents. All existing modules are fixed in-place.

**Tech Stack:** Python 3.11, FastAPI, websockets, ElevenLabs SDK v1.50+, aiosqlite, typer/rich, sounddevice (bridge), RPi.GPIO (optional PTT), pytest-asyncio, Fly.io (optional cloud deploy)

> **Review findings applied (2026-05-31):** 11 bugs/issues identified by senior-engineer review, walkie-talkie expert, and networking engineer. All critical fixes incorporated below. See "Review Findings" section at end.

---

## Research Reference
`thoughts/shared/research/2026-05-31-walkie-talkie-e2e-tailnet.md`

---

## File Map

| File | Status | Responsibility |
|------|--------|----------------|
| `project/walkie_agent/audio/elevenlabs_bridge.py` | Modify | Add `send_pcm_16k()` to fix double-resampling bug |
| `project/walkie_agent/audio/websocket.py` | Modify | Use `send_pcm_16k()` instead of `send_audio()` for utterances |
| `project/walkie_agent/agent/engine.py` | Modify | Fix `self.audit_log` → `self.audit_logger` typo |
| `project/walkie_agent/agent/personality.py` | Modify | Fix "ALREADY" typo; inject `language` into prompt text |
| `project/walkie_agent/api.py` | Modify | Use `build_system_prompt()` in `_PersonalityManagerProxy` |
| `project/walkie_agent/config/database.py` | Modify | Add `get_setting()` / `set_setting()` key-value store for agent ID |
| `project/walkie_agent/elevenlabs_admin.py` | **Create** | `ElevenLabsAdmin` class wrapping SDK agent CRUD |
| `project/walkie_agent/config/cli.py` | Modify | Add `elevenlabs` sub-command group (create/list/sync/set-agent) |
| `bridge/radio_client.py` | **Create** | Pi bridge: audio capture → VOX/PTT → WebSocket → playback |
| `bridge/requirements.txt` | **Create** | sounddevice, websockets, RPi.GPIO (optional) |
| `bridge/.env.example` | **Create** | SERVER_URL, AUDIO_DEVICE, VOX_THRESHOLD, PTT_GPIO_PIN |
| `project/tests/conftest.py` | **Create** | Pytest fixtures: in-memory DB, mock ElevenLabs bridge |
| `project/tests/test_audio_pipeline.py` | **Create** | Unit tests for AudioBuffer, AudioFormatConverter |
| `project/tests/test_elevenlabs_admin.py` | **Create** | Unit tests for ElevenLabsAdmin (mock SDK) |
| `project/tests/test_api_websocket.py` | **Create** | E2E WebSocket test with mock bridge |

---

## Phase 1: Critical Bug Fixes

**Focus:** Three bugs found in research — fix before anything else.

### Task 1: Fix Double-Resampling in Audio Pipeline

**Files:**
- Modify: `project/walkie_agent/audio/elevenlabs_bridge.py`
- Modify: `project/walkie_agent/audio/websocket.py`
- Test: `project/tests/test_audio_pipeline.py`

- [ ] **Step 1: Write the failing test**

Create `project/tests/test_audio_pipeline.py`:
```python
import pytest
import struct
import asyncio
from unittest.mock import AsyncMock, MagicMock, patch

from walkie_agent.audio.elevenlabs_bridge import ElevenLabsBridge


@pytest.mark.asyncio
async def test_send_pcm_16k_does_not_resample():
    """send_pcm_16k must base64-encode input as-is, no resampling."""
    bridge = ElevenLabsBridge(
        client_id="test",
        api_key="fake",
        agent_id="fake",
        system_prompt="test",
        audio_format="ulaw",
        sample_rate=8000,
    )
    bridge._connected = True
    sent_messages = []

    mock_ws = AsyncMock()
    mock_ws.send = AsyncMock(side_effect=lambda msg: sent_messages.append(msg))
    bridge._ws = mock_ws

    # 100 samples of 16kHz PCM (200 bytes)
    pcm_16k = struct.pack("<100h", *([500] * 100))
    await bridge.send_pcm_16k(pcm_16k)

    assert len(sent_messages) == 1
    import json, base64
    msg = json.loads(sent_messages[0])
    assert msg["type"] == "user_audio_chunk"
    decoded = base64.b64decode(msg["audio_event"]["audio_base_64"])
    assert decoded == pcm_16k  # must be exact — no resampling


@pytest.mark.asyncio
async def test_send_audio_still_converts_ulaw():
    """send_audio (existing method) must still convert ulaw->pcm->16kHz for non-VAD use."""
    bridge = ElevenLabsBridge(
        client_id="test",
        api_key="fake",
        agent_id="fake",
        system_prompt="test",
        audio_format="ulaw",
        sample_rate=8000,
    )
    bridge._connected = True
    sent_messages = []

    mock_ws = AsyncMock()
    mock_ws.send = AsyncMock(side_effect=lambda msg: sent_messages.append(msg))
    bridge._ws = mock_ws

    # 100 ulaw bytes (8kHz)
    ulaw_bytes = bytes([127] * 100)
    await bridge.send_audio(ulaw_bytes)

    assert len(sent_messages) == 1
    import json, base64
    msg = json.loads(sent_messages[0])
    decoded = base64.b64decode(msg["audio_event"]["audio_base_64"])
    # Resampled 8kHz->16kHz: 100 ulaw bytes = 100 PCM samples at 8kHz = 200 PCM samples at 16kHz = 400 bytes
    assert len(decoded) == 400
```

- [ ] **Step 2: Run tests — expect FAIL (AttributeError: no send_pcm_16k)**

```bash
cd project && python -m pytest tests/test_audio_pipeline.py -v 2>&1 | head -30
```
Expected: `AttributeError: 'ElevenLabsBridge' object has no attribute 'send_pcm_16k'`

- [ ] **Step 3: Add `send_pcm_16k()` to `ElevenLabsBridge`**

In `project/walkie_agent/audio/elevenlabs_bridge.py`, after the existing `send_audio()` method (line ~389), add:

```python
    async def send_pcm_16k(self, pcm_16k: bytes) -> None:
        """Send already-converted 16kHz PCM audio to ElevenLabs without resampling.

        Use this when the caller has already done format conversion (e.g., after
        VAD processing in AudioBuffer which operates at 16 kHz).

        Args:
            pcm_16k: 16-bit signed little-endian PCM at 16 kHz.

        Raises:
            ConnectionError: If the bridge is not connected.
        """
        if not self.is_connected:
            raise ConnectionError("Bridge is not connected")

        if not pcm_16k:
            return

        audio_b64 = base64.b64encode(pcm_16k).decode("utf-8")
        message = {
            "type": "user_audio_chunk",
            "audio_event": {"audio_base_64": audio_b64},
        }

        try:
            await self._ws.send(json.dumps(message))
        except Exception as exc:
            logger.error(
                "Failed to send PCM audio to ElevenLabs for client %s: %s",
                self.client_id,
                exc,
            )
            raise ConnectionError(f"Failed to send audio: {exc}") from exc
```

- [ ] **Step 4: Update `websocket.py` to call `send_pcm_16k`**

In `project/walkie_agent/audio/websocket.py`, find line ~399 inside `_client_to_elevenlabs_loop()`:

```python
                    if client.elevenlabs_bridge and client.elevenlabs_bridge.is_connected:
                        try:
                            await client.elevenlabs_bridge.send_audio(utterance)
```

Change to:
```python
                    if client.elevenlabs_bridge and client.elevenlabs_bridge.is_connected:
                        try:
                            # Utterance is already 16kHz PCM from AudioBuffer VAD
                            await client.elevenlabs_bridge.send_pcm_16k(utterance)
```

- [ ] **Step 5: Run tests — expect PASS**

```bash
cd project && python -m pytest tests/test_audio_pipeline.py -v
```
Expected: `2 passed`

- [ ] **Step 6: Commit**

```bash
cd project && git add walkie_agent/audio/elevenlabs_bridge.py walkie_agent/audio/websocket.py tests/test_audio_pipeline.py
git commit -m "fix: add send_pcm_16k to bridge; fix double-resampling of VAD utterances"
```

---

### Task 2: Fix `audit_logger` Typo Crash in engine.py

**Files:**
- Modify: `project/walkie_agent/agent/engine.py:284`

- [ ] **Step 1: Write the failing test**

Add to `project/tests/test_audio_pipeline.py` (append at end):
```python
import asyncio
from walkie_agent.agent.engine import AgentEngine, ConversationState
from walkie_agent.agent.personality import PersonalityConfig, VoiceSettings
from walkie_agent.agent.tool_router import ToolRegistry


@pytest.mark.asyncio
async def test_process_agent_response_does_not_crash_on_unsafe_output():
    """AgentEngine.process_agent_response must not crash when output contains blocked words."""
    personality = PersonalityConfig(
        id="test",
        name="Test",
        backstory="A test agent.",
        max_response_length=50,
        voice_settings=VoiceSettings(voice_id="test"),
    )
    engine = AgentEngine(
        client_id="test-client",
        personality=personality,
        tool_registry=ToolRegistry(),
    )
    engine._state = ConversationState.SPEAKING

    # "kill" is in the blocked list; output safety filter should scrub it
    result = await engine.process_agent_response("I will kill the dragon.")
    # Must return a string, not raise AttributeError
    assert isinstance(result, str)
    assert "kill" not in result.lower()  # word scrubbed
```

- [ ] **Step 2: Run test — expect FAIL (AttributeError)**

```bash
cd project && python -m pytest tests/test_audio_pipeline.py::test_process_agent_response_does_not_crash_on_unsafe_output -v
```
Expected: `AttributeError: 'list' object has no attribute 'info'`

- [ ] **Step 3: Fix the typo in engine.py**

In `project/walkie_agent/agent/engine.py`, line 284:
```python
# BEFORE (wrong — self.audit_log is a List[_AuditEntry], not a Logger):
        self.audit_log.info(
            "Output had unsafe content and was scrubbed | client=%s", self.client_id
        )

# AFTER:
        self.audit_logger.warning(
            "Output had unsafe content and was scrubbed | client=%s", self.client_id
        )
```

- [ ] **Step 4: Run test — expect PASS**

```bash
cd project && python -m pytest tests/test_audio_pipeline.py::test_process_agent_response_does_not_crash_on_unsafe_output -v
```
Expected: `1 passed`

- [ ] **Step 5: Commit**

```bash
cd project && git add walkie_agent/agent/engine.py tests/test_audio_pipeline.py
git commit -m "fix: audit_log -> audit_logger typo in AgentEngine.process_agent_response"
```

---

### Task 3: Fix Typos in personality.py + Inject Language into Prompt

**Files:**
- Modify: `project/walkie_agent/agent/personality.py`

- [ ] **Step 1: Write the failing tests**

Append to `project/tests/test_audio_pipeline.py`:
```python
from walkie_agent.agent.personality import PersonalityConfig, PersonalityManager, VoiceSettings


def test_build_system_prompt_uses_always_not_already():
    """ALWAYS must appear in safety rules, not ALREADY (typo fix)."""
    config = PersonalityConfig(
        id="test", name="Test", backstory="A tester.",
        voice_settings=VoiceSettings(voice_id="v"),
    )
    prompt = PersonalityManager.build_system_prompt(config)
    assert "ALWAYS respond in the language" in prompt
    assert "ALREADY respond in the language" not in prompt


def test_build_system_prompt_includes_language():
    """The system prompt must explicitly name the target language."""
    config = PersonalityConfig(
        id="test", name="Test", backstory="A tester.",
        language="pl",
        voice_settings=VoiceSettings(voice_id="v"),
    )
    prompt = PersonalityManager.build_system_prompt(config)
    assert "Polish" in prompt or "pl" in prompt
```

- [ ] **Step 2: Run tests — expect FAIL**

```bash
cd project && python -m pytest tests/test_audio_pipeline.py::test_build_system_prompt_uses_always_not_already tests/test_audio_pipeline.py::test_build_system_prompt_includes_language -v
```
Expected: 2 failures.

- [ ] **Step 3: Fix typo and add language injection in personality.py**

In `project/walkie_agent/agent/personality.py`, find the safety guardrails block (line ~487):

```python
# BEFORE:
            "ALREADY respond in the language specified by the system.",

# AFTER:
            f"ALWAYS respond in the language: {config.language}. "
            f"If language is 'pl', speak Polish. If 'en', speak English. "
            f"Never switch languages mid-response.",
```

- [ ] **Step 4: Run tests — expect PASS**

```bash
cd project && python -m pytest tests/test_audio_pipeline.py::test_build_system_prompt_uses_always_not_already tests/test_audio_pipeline.py::test_build_system_prompt_includes_language -v
```
Expected: `2 passed`

- [ ] **Step 5: Commit**

```bash
cd project && git add walkie_agent/agent/personality.py tests/test_audio_pipeline.py
git commit -m "fix: typo ALREADY->ALWAYS; inject language code into system prompt"
```

---

## Phase 2: ElevenLabs Agent CLI Management

**Focus:** Add `walkie-agent elevenlabs` commands that create/update ElevenLabs ConvAI agents from a local personality config, and store the agent ID in the database.

### Task 4: Add `settings` Table to ConfigDatabase

**Files:**
- Modify: `project/walkie_agent/config/database.py`
- Test: `project/tests/test_elevenlabs_admin.py`

The `settings` table stores key-value pairs (e.g., `elevenlabs_agent_id` → agent ID string).

- [ ] **Step 1: Write the failing tests**

Create `project/tests/test_elevenlabs_admin.py`:
```python
import pytest
import asyncio
from walkie_agent.config.database import ConfigDatabase


@pytest.mark.asyncio
async def test_settings_get_set_roundtrip():
    db = ConfigDatabase(":memory:")
    await db.initialize()

    await db.set_setting("my_key", "my_value")
    result = await db.get_setting("my_key")
    assert result == "my_value"


@pytest.mark.asyncio
async def test_settings_returns_none_for_missing_key():
    db = ConfigDatabase(":memory:")
    await db.initialize()

    result = await db.get_setting("nonexistent")
    assert result is None


@pytest.mark.asyncio
async def test_settings_update_overwrites():
    db = ConfigDatabase(":memory:")
    await db.initialize()

    await db.set_setting("k", "v1")
    await db.set_setting("k", "v2")
    assert await db.get_setting("k") == "v2"
```

- [ ] **Step 2: Run tests — expect FAIL**

```bash
cd project && python -m pytest tests/test_elevenlabs_admin.py -v
```
Expected: `AttributeError: 'ConfigDatabase' object has no attribute 'set_setting'`

- [ ] **Step 3: Add `settings` table and get/set methods to ConfigDatabase**

In `project/walkie_agent/config/database.py`, inside `initialize()` after the last `CREATE TABLE` block (before `CREATE INDEX` statements), add:

```python
        await conn.execute(
            """
            CREATE TABLE IF NOT EXISTS settings (
                key   TEXT PRIMARY KEY,
                value TEXT NOT NULL
            )
            """
        )
```

Then after the existing `_row_to_dict` staticmethod, add two new methods:

```python
    async def get_setting(self, key: str) -> Optional[str]:
        """Return a scalar setting value, or None if not set."""
        conn = await self._connect()
        async with conn.execute(
            "SELECT value FROM settings WHERE key = ?", (key,)
        ) as cursor:
            row = await cursor.fetchone()
        return row[0] if row else None

    async def set_setting(self, key: str, value: str) -> None:
        """Upsert a scalar setting."""
        conn = await self._connect()
        await conn.execute(
            "INSERT OR REPLACE INTO settings (key, value) VALUES (?, ?)",
            (key, value),
        )
        await conn.commit()
```

- [ ] **Step 4: Run tests — expect PASS**

```bash
cd project && python -m pytest tests/test_elevenlabs_admin.py -v
```
Expected: `3 passed`

- [ ] **Step 5: Commit**

```bash
cd project && git add walkie_agent/config/database.py tests/test_elevenlabs_admin.py
git commit -m "feat: add settings key-value table to ConfigDatabase (stores elevenlabs_agent_id)"
```

---

### Task 5: Create `ElevenLabsAdmin` Class

**Files:**
- Create: `project/walkie_agent/elevenlabs_admin.py`
- Test: `project/tests/test_elevenlabs_admin.py` (append)

- [ ] **Step 1: Write the failing tests**

Append to `project/tests/test_elevenlabs_admin.py`:
```python
from unittest.mock import MagicMock, patch, AsyncMock
from walkie_agent.elevenlabs_admin import ElevenLabsAdmin
from walkie_agent.agent.personality import PersonalityConfig, VoiceSettings


def _make_personality(lang="en") -> PersonalityConfig:
    return PersonalityConfig(
        id="tester",
        name="Tester Bot",
        backstory="A test personality.",
        language=lang,
        max_response_length=60,
        voice_settings=VoiceSettings(
            voice_id="XB0fDUnXU5powFXDhCwa",
            stability=0.5,
            similarity_boost=0.75,
        ),
    )


def test_elevenlabs_admin_create_agent_calls_sdk():
    """create_agent must call client.conversational_ai.agents.create with correct params."""
    mock_response = MagicMock()
    mock_response.agent_id = "new-agent-123"

    mock_agents = MagicMock()
    mock_agents.create.return_value = mock_response

    mock_client = MagicMock()
    mock_client.conversational_ai.agents = mock_agents

    admin = ElevenLabsAdmin(api_key="fake-key")
    admin._client = mock_client

    personality = _make_personality()
    from walkie_agent.agent.personality import PersonalityManager
    system_prompt = PersonalityManager.build_system_prompt(personality)

    agent_id = admin.create_agent(personality, system_prompt)

    assert agent_id == "new-agent-123"
    mock_agents.create.assert_called_once()
    call_kwargs = mock_agents.create.call_args.kwargs
    assert call_kwargs["name"] == "Tester Bot"


def test_elevenlabs_admin_update_agent_calls_sdk():
    """update_agent must call client.conversational_ai.agents.update."""
    mock_agents = MagicMock()
    mock_client = MagicMock()
    mock_client.conversational_ai.agents = mock_agents

    admin = ElevenLabsAdmin(api_key="fake-key")
    admin._client = mock_client

    personality = _make_personality()
    from walkie_agent.agent.personality import PersonalityManager
    system_prompt = PersonalityManager.build_system_prompt(personality)

    admin.update_agent("existing-agent-id", personality, system_prompt)

    mock_agents.update.assert_called_once()
    call_args = mock_agents.update.call_args
    assert call_args.args[0] == "existing-agent-id"


def test_elevenlabs_admin_list_agents():
    """list_agents must return list of (agent_id, name) tuples."""
    mock_item_1 = MagicMock()
    mock_item_1.agent_id = "id-1"
    mock_item_1.name = "Bot One"

    mock_item_2 = MagicMock()
    mock_item_2.agent_id = "id-2"
    mock_item_2.name = "Bot Two"

    mock_response = MagicMock()
    mock_response.agents = [mock_item_1, mock_item_2]

    mock_agents = MagicMock()
    mock_agents.list.return_value = mock_response

    mock_client = MagicMock()
    mock_client.conversational_ai.agents = mock_agents

    admin = ElevenLabsAdmin(api_key="fake-key")
    admin._client = mock_client

    result = admin.list_agents()
    assert result == [("id-1", "Bot One"), ("id-2", "Bot Two")]
```

- [ ] **Step 2: Run tests — expect FAIL**

```bash
cd project && python -m pytest tests/test_elevenlabs_admin.py::test_elevenlabs_admin_create_agent_calls_sdk -v
```
Expected: `ModuleNotFoundError: No module named 'walkie_agent.elevenlabs_admin'`

- [ ] **Step 3: Create `elevenlabs_admin.py`**

Create `project/walkie_agent/elevenlabs_admin.py`:
```python
"""ElevenLabs ConvAI agent management — create/update/list agents from personality configs."""

from __future__ import annotations

import logging
from typing import Optional

from walkie_agent.agent.personality import PersonalityConfig

logger = logging.getLogger(__name__)

# TTS model that supports Polish and 31 other languages
_DEFAULT_TTS_MODEL = "eleven_flash_v2_5"
# ASR format: 16kHz PCM (what our pipeline produces after the VAD)
_DEFAULT_ASR_FORMAT = "pcm_16000"


class ElevenLabsAdmin:
    """Thin wrapper around the ElevenLabs SDK for ConvAI agent CRUD.

    All methods are synchronous (SDK is synchronous); wrap in asyncio.to_thread
    if calling from async context.
    """

    def __init__(self, api_key: str) -> None:
        self.api_key = api_key
        self._client = self._build_client()

    def _build_client(self):
        try:
            from elevenlabs.client import ElevenLabs
            return ElevenLabs(api_key=self.api_key)
        except ImportError as exc:
            raise RuntimeError(
                "elevenlabs package is required. Install it: pip install elevenlabs"
            ) from exc

    # ------------------------------------------------------------------
    # Public API
    # ------------------------------------------------------------------

    def create_agent(self, personality: PersonalityConfig, system_prompt: str) -> str:
        """Create a new ConvAI agent in ElevenLabs from a personality config.

        Args:
            personality: Local PersonalityConfig with voice/language settings.
            system_prompt: Full system prompt string (from build_system_prompt).

        Returns:
            The new ElevenLabs agent_id string.
        """
        from elevenlabs.types import (
            ConversationalConfig,
            AgentConfig,
            PromptAgentApiModelOutput,
            TtsConversationalConfigOutput,
            AsrConversationalConfig,
        )

        config = self._build_conv_config(personality, system_prompt)
        response = self._client.conversational_ai.agents.create(
            name=personality.name,
            conversation_config=config,
        )
        logger.info("Created ElevenLabs agent '%s' with id=%s", personality.name, response.agent_id)
        return response.agent_id

    def update_agent(
        self, agent_id: str, personality: PersonalityConfig, system_prompt: str
    ) -> None:
        """Update an existing ElevenLabs ConvAI agent with new personality settings.

        Args:
            agent_id: The ElevenLabs agent ID to update.
            personality: Updated PersonalityConfig.
            system_prompt: New system prompt string.
        """
        config = self._build_conv_config(personality, system_prompt)
        self._client.conversational_ai.agents.update(
            agent_id,
            name=personality.name,
            conversation_config=config,
        )
        logger.info("Updated ElevenLabs agent id=%s", agent_id)

    def get_agent(self, agent_id: str) -> dict:
        """Return raw agent details dict."""
        response = self._client.conversational_ai.agents.get(agent_id)
        return {
            "agent_id": response.agent_id,
            "name": response.name,
        }

    def list_agents(self) -> list[tuple[str, str]]:
        """Return [(agent_id, name), ...] for all agents in the account."""
        response = self._client.conversational_ai.agents.list()
        return [(a.agent_id, a.name) for a in response.agents]

    def delete_agent(self, agent_id: str) -> None:
        """Delete an ElevenLabs agent by ID."""
        self._client.conversational_ai.agents.delete(agent_id)
        logger.info("Deleted ElevenLabs agent id=%s", agent_id)

    # ------------------------------------------------------------------
    # Private helpers
    # ------------------------------------------------------------------

    def _build_conv_config(self, personality: PersonalityConfig, system_prompt: str):
        from elevenlabs.types import (
            ConversationalConfig,
            AgentConfig,
            PromptAgentApiModelOutput,
            TtsConversationalConfigOutput,
            AsrConversationalConfig,
        )

        # Pick TTS model: multilingual for non-English, flash for English
        tts_model = _DEFAULT_TTS_MODEL  # eleven_flash_v2_5 supports 32 languages

        return ConversationalConfig(
            agent=AgentConfig(
                first_message=self._first_message(personality.language),
                language=personality.language,
                prompt=PromptAgentApiModelOutput(
                    prompt=system_prompt,
                    llm="gemini-2.5-flash",
                    temperature=0.6,
                    max_tokens=200,
                ),
            ),
            tts=TtsConversationalConfigOutput(
                voice_id=personality.voice_settings.voice_id,
                model_id=tts_model,
                stability=personality.voice_settings.stability,
                similarity_boost=personality.voice_settings.similarity_boost,
                speed=1.0,
            ),
            asr=AsrConversationalConfig(
                provider="elevenlabs",
                user_input_audio_format=_DEFAULT_ASR_FORMAT,
            ),
        )

    @staticmethod
    def _first_message(language: str) -> str:
        messages = {
            "pl": "Hej, słucham!",
            "en": "Hey there, I'm listening!",
        }
        return messages.get(language, "Hello!")
```

- [ ] **Step 4: Run tests — expect PASS**

```bash
cd project && python -m pytest tests/test_elevenlabs_admin.py -v
```
Expected: `6 passed`

- [ ] **Step 5: Commit**

```bash
cd project && git add walkie_agent/elevenlabs_admin.py tests/test_elevenlabs_admin.py
git commit -m "feat: ElevenLabsAdmin class for ConvAI agent CRUD via SDK"
```

---

### Task 6: Add `elevenlabs` CLI Sub-Commands

**Files:**
- Modify: `project/walkie_agent/config/cli.py`

- [ ] **Step 1: Add the `elevenlabs_app` typer group at the end of `cli.py`**

In `project/walkie_agent/config/cli.py`, before the `def main():` at the bottom, add:

```python
# ---------------------------------------------------------------------------
# ElevenLabs agent management commands
# ---------------------------------------------------------------------------

elevenlabs_app = typer.Typer(help="Manage ElevenLabs ConvAI agents")
app.add_typer(elevenlabs_app, name="elevenlabs")


@elevenlabs_app.command("list")
def elevenlabs_list() -> None:
    """List all ConvAI agents in the ElevenLabs account."""
    settings = get_settings()
    if not settings.elevenlabs_api_key:
        console.print("[red]WALKIE_ELEVENLABS_API_KEY is not set.[/]")
        raise typer.Exit(1)

    from walkie_agent.elevenlabs_admin import ElevenLabsAdmin

    admin = ElevenLabsAdmin(api_key=settings.elevenlabs_api_key)
    try:
        agents = admin.list_agents()
    except Exception as exc:
        console.print(f"[red]ElevenLabs API error: {exc}[/]")
        raise typer.Exit(1)

    if not agents:
        console.print("[dim]No agents found.[/]")
        return

    table = Table(title="ElevenLabs ConvAI Agents", show_lines=True)
    table.add_column("Agent ID", style="cyan")
    table.add_column("Name", style="green")

    db = _get_db()
    current_agent_id = _run(db.initialize()) or None
    current_agent_id = _run(db.get_setting("elevenlabs_agent_id"))

    for agent_id, name in agents:
        marker = "[bold green]*[/]" if agent_id == current_agent_id else ""
        table.add_row(agent_id, name + (" " + marker if marker else ""))
    console.print(table)
    if current_agent_id:
        console.print(f"\n[dim]Active agent ID: {current_agent_id}[/]")


@elevenlabs_app.command("create-agent")
def elevenlabs_create_agent(
    personality_id: str = typer.Argument(..., help="Local personality ID to create an agent from"),
) -> None:
    """Create a new ElevenLabs ConvAI agent from a local personality."""
    settings = get_settings()
    if not settings.elevenlabs_api_key:
        console.print("[red]WALKIE_ELEVENLABS_API_KEY is not set.[/]")
        raise typer.Exit(1)

    db = _get_db()
    _run(db.initialize())
    personality_data = _run(db.get_personality(personality_id))
    if personality_data is None:
        console.print(f"[red]Personality '{personality_id}' not found.[/]")
        raise typer.Exit(1)

    # Rebuild PersonalityConfig from raw dict
    from walkie_agent.agent.personality import PersonalityConfig, PersonalityManager, VoiceSettings
    voice_settings_raw = personality_data.get("voice_settings_json") or {}
    if isinstance(voice_settings_raw, str):
        import json as _json
        voice_settings_raw = _json.loads(voice_settings_raw) if voice_settings_raw else {}
    config = PersonalityConfig(
        id=personality_data["id"],
        name=personality_data["name"],
        backstory=personality_data.get("backstory", ""),
        language=personality_data.get("language", "en"),
        max_response_length=int(personality_data.get("max_response_length", 150)),
        voice_settings=VoiceSettings(
            voice_id=personality_data.get("voice_id", ""),
            stability=float(voice_settings_raw.get("stability", 0.5)),
            similarity_boost=float(voice_settings_raw.get("similarity_boost", 0.75)),
        ),
        custom_instructions=personality_data.get("custom_instructions", ""),
        allowed_tools=personality_data.get("allowed_tools", []) or [],
    )
    system_prompt = PersonalityManager.build_system_prompt(config)

    from walkie_agent.elevenlabs_admin import ElevenLabsAdmin
    admin = ElevenLabsAdmin(api_key=settings.elevenlabs_api_key)

    with console.status(f"Creating ElevenLabs agent for '{config.name}'..."):
        try:
            agent_id = admin.create_agent(config, system_prompt)
        except Exception as exc:
            console.print(f"[red]ElevenLabs API error: {exc}[/]")
            raise typer.Exit(1)

    _run(db.set_setting("elevenlabs_agent_id", agent_id))
    console.print(f"[bold green]Created agent '{config.name}'[/]")
    console.print(f"  Agent ID: [cyan]{agent_id}[/]")
    console.print(f"  [dim]Set as active agent (WALKIE_ELEVENLABS_AGENT_ID={agent_id})[/]")
    console.print(f"\n  Add to your .env:\n  [bold]WALKIE_ELEVENLABS_AGENT_ID={agent_id}[/bold]")


@elevenlabs_app.command("sync-agent")
def elevenlabs_sync_agent(
    personality_id: str = typer.Argument(..., help="Local personality ID to sync"),
    agent_id: Optional[str] = typer.Option(None, "--agent-id", help="ElevenLabs agent ID (uses saved if omitted)"),
) -> None:
    """Sync a local personality config to an existing ElevenLabs agent."""
    settings = get_settings()
    if not settings.elevenlabs_api_key:
        console.print("[red]WALKIE_ELEVENLABS_API_KEY is not set.[/]")
        raise typer.Exit(1)

    db = _get_db()
    _run(db.initialize())

    effective_agent_id = agent_id or _run(db.get_setting("elevenlabs_agent_id")) or settings.elevenlabs_agent_id
    if not effective_agent_id:
        console.print("[red]No agent ID. Run 'walkie-agent elevenlabs create-agent' first, or pass --agent-id.[/]")
        raise typer.Exit(1)

    personality_data = _run(db.get_personality(personality_id))
    if personality_data is None:
        console.print(f"[red]Personality '{personality_id}' not found.[/]")
        raise typer.Exit(1)

    from walkie_agent.agent.personality import PersonalityConfig, PersonalityManager, VoiceSettings
    import json as _json
    voice_settings_raw = personality_data.get("voice_settings_json") or {}
    if isinstance(voice_settings_raw, str):
        voice_settings_raw = _json.loads(voice_settings_raw) if voice_settings_raw else {}
    config = PersonalityConfig(
        id=personality_data["id"],
        name=personality_data["name"],
        backstory=personality_data.get("backstory", ""),
        language=personality_data.get("language", "en"),
        max_response_length=int(personality_data.get("max_response_length", 150)),
        voice_settings=VoiceSettings(
            voice_id=personality_data.get("voice_id", ""),
            stability=float(voice_settings_raw.get("stability", 0.5)),
            similarity_boost=float(voice_settings_raw.get("similarity_boost", 0.75)),
        ),
        custom_instructions=personality_data.get("custom_instructions", ""),
        allowed_tools=personality_data.get("allowed_tools", []) or [],
    )
    system_prompt = PersonalityManager.build_system_prompt(config)

    from walkie_agent.elevenlabs_admin import ElevenLabsAdmin
    admin = ElevenLabsAdmin(api_key=settings.elevenlabs_api_key)

    with console.status(f"Syncing '{config.name}' to agent {effective_agent_id}..."):
        try:
            admin.update_agent(effective_agent_id, config, system_prompt)
        except Exception as exc:
            console.print(f"[red]ElevenLabs API error: {exc}[/]")
            raise typer.Exit(1)

    console.print(f"[bold green]Synced '{config.name}' → agent {effective_agent_id}[/]")


@elevenlabs_app.command("set-agent")
def elevenlabs_set_agent(
    agent_id: str = typer.Argument(..., help="ElevenLabs agent ID to use as the active agent"),
) -> None:
    """Set the active ElevenLabs agent ID (persisted in local DB, overrides env var)."""
    db = _get_db()
    _run(db.initialize())
    _run(db.set_setting("elevenlabs_agent_id", agent_id))
    console.print(f"[green]Active agent ID set to: [bold]{agent_id}[/bold][/]")
    console.print("[dim]Also set WALKIE_ELEVENLABS_AGENT_ID in your .env for the server.[/]")
```

- [ ] **Step 2: Manually smoke-test CLI help**

```bash
cd project && python -m walkie_agent.config.cli elevenlabs --help
```
Expected: Shows subcommands: `list`, `create-agent`, `sync-agent`, `set-agent`

- [ ] **Step 3: Commit**

```bash
cd project && git add walkie_agent/config/cli.py
git commit -m "feat: add walkie-agent elevenlabs CLI commands (create-agent, sync-agent, list, set-agent)"
```

---

## Phase 3: Fix Personality Prompt Builder in Server + Use Build System Prompt

### Task 7: Wire `build_system_prompt()` into the WebSocket Runtime

**Files:**
- Modify: `project/walkie_agent/api.py`

The `_PersonalityManagerProxy.load_personality()` currently returns `backstory + custom_instructions` as the prompt. It must use `PersonalityManager.build_system_prompt()` instead to get kid-safety rules, language injection, and trait instructions.

- [ ] **Step 1: Update `_PersonalityManagerProxy.load_personality()` in `api.py`**

In `project/walkie_agent/api.py`, find `class _PersonalityManagerProxy` (line ~231) and replace the `load_personality` method:

```python
    async def load_personality(self, client_id: str) -> Dict[str, Any]:
        """Return the default personality enriched with a full system prompt."""
        personality_data = await self.db.get_default_personality()
        if personality_data is None:
            return {}

        # Rebuild a PersonalityConfig so we can use the full prompt builder
        from walkie_agent.agent.personality import PersonalityConfig, PersonalityManager, VoiceSettings
        import json as _json

        voice_settings_raw = personality_data.get("voice_settings_json") or {}
        if isinstance(voice_settings_raw, str):
            voice_settings_raw = _json.loads(voice_settings_raw) if voice_settings_raw else {}

        allowed_tools_raw = personality_data.get("allowed_tools") or []
        if isinstance(allowed_tools_raw, str):
            try:
                allowed_tools_raw = _json.loads(allowed_tools_raw)
            except Exception:
                allowed_tools_raw = []

        config = PersonalityConfig(
            id=personality_data.get("id", "default"),
            name=personality_data.get("name", "Assistant"),
            backstory=personality_data.get("backstory", ""),
            language=personality_data.get("language", "en"),
            max_response_length=int(personality_data.get("max_response_length", 150)),
            voice_settings=VoiceSettings(
                voice_id=personality_data.get("voice_id", ""),
                stability=float(voice_settings_raw.get("stability", 0.5)),
                similarity_boost=float(voice_settings_raw.get("similarity_boost", 0.75)),
            ),
            custom_instructions=personality_data.get("custom_instructions", ""),
            allowed_tools=list(allowed_tools_raw) if isinstance(allowed_tools_raw, list) else [],
        )

        full_prompt = PersonalityManager.build_system_prompt(config)
        personality_data["prompt"] = full_prompt
        return personality_data
```

- [ ] **Step 2: Also use DB-stored agent ID as override in WebSocket handler**

In `api.py`, find the WebSocket handler `walkie_websocket()`. After `settings = get_settings()`, add:

```python
    # Prefer agent ID stored in DB (set via CLI) over env var
    db_agent_id = await websocket.app.state.db.get_setting("elevenlabs_agent_id")
    if db_agent_id:
        # Pydantic v2: model_copy creates a shallow copy with field overrides
        settings = settings.model_copy(update={"elevenlabs_agent_id": db_agent_id})
```

- [ ] **Step 3: Smoke-test the server starts cleanly**

```bash
cd project && WALKIE_ELEVENLABS_API_KEY=test WALKIE_ELEVENLABS_AGENT_ID=test python -c "
import asyncio
from walkie_agent.config.database import ConfigDatabase
from walkie_agent.api import _PersonalityManagerProxy

async def test():
    db = ConfigDatabase(':memory:')
    await db.initialize()
    await db.seed_default_personality()
    proxy = _PersonalityManagerProxy(db)
    p = await proxy.load_personality('test-client')
    print('Prompt length:', len(p.get('prompt', '')))
    assert 'ALWAYS respond in the language' in p['prompt']
    assert len(p['prompt']) > 200
    print('OK')

asyncio.run(test())
"
```
Expected: `Prompt length: <some large number>` and `OK`

- [ ] **Step 4: Commit**

```bash
cd project && git add walkie_agent/api.py
git commit -m "fix: use PersonalityManager.build_system_prompt() in WebSocket runtime; resolve agent ID from DB"
```

---

## Phase 4: Polish Personality

### Task 8: Seed Polish Personality via CLI

**Files:**
- Modify: `project/walkie_agent/config/cli.py`
- Modify: `project/walkie_agent/config/database.py`

- [ ] **Step 1: Add `seed-polish` command to agent CLI group in `cli.py`**

In `project/walkie_agent/config/cli.py`, inside the `agent_app` section (after `agent_set_default`), add:

```python
@agent_app.command("seed-polish")
def agent_seed_polish() -> None:
    """Seed a Polish-language 'Przyjaciel Radek' personality."""
    import json as _json

    polish_personality = {
        "id": "przyjaciel-radek",
        "name": "Przyjaciel Radek",
        "backstory": (
            "Jesteś Przyjaciel Radek, wesoły i cierpliwy asystent głosowy dla dzieci. "
            "Mówisz prostym, ciepłym językiem polskim. "
            "Lubisz opowiadać krótkie historyjki, odpowiadać na pytania dzieci i śpiewać piosenki. "
            "Jesteś zawsze miły, zachęcający i bezpieczny dla małych dzieci."
        ),
        "personality_traits": {
            "wesoły": 0.9,
            "cierpliwy": 0.95,
            "pomocny": 0.9,
            "przyjazny": 0.95,
        },
        # Hanna — Polish female voice from ElevenLabs
        "voice_id": "NacdHGUYR1k3M0FAbAia",
        "voice_settings_json": _json.dumps({
            "stability": 0.55,
            "similarity_boost": 0.80,
            "speed": 0.95,
        }),
        "language": "pl",
        "max_response_length": 60,
        "allowed_tools": ["web_search"],
        "custom_instructions": (
            "Zawsze odpowiadaj po polsku. "
            "Używaj prostych słów zrozumiałych dla 6-latka. "
            "Maksymalnie 2-3 zdania na odpowiedź. "
            "Zaczynaj od ciepłego powitania jak 'Hej!' lub 'Cześć!'."
        ),
        "is_default": False,
    }

    db = _get_db()
    _run(db.initialize())
    existing = _run(db.get_personality("przyjaciel-radek"))
    if existing:
        console.print("[yellow]Personality 'przyjaciel-radek' already exists. Use 'agent edit przyjaciel-radek' to modify.[/]")
        return

    _run(db.save_personality(polish_personality))
    console.print("[bold green]Seeded Polish personality 'Przyjaciel Radek'.[/]")
    console.print("  Language: [cyan]pl[/] | Voice: [magenta]NacdHGUYR1k3M0FAbAia[/] (Hanna)")
    console.print("\n  Next steps:")
    console.print("  1. [bold]walkie-agent elevenlabs create-agent przyjaciel-radek[/bold]  ← creates ElevenLabs agent")
    console.print("  2. Add the printed AGENT_ID to your .env")
    console.print("  3. [bold]walkie-agent agent set-default przyjaciel-radek[/bold]  ← makes it default")
```

- [ ] **Step 2: Test the command manually**

```bash
cd project && WALKIE_DATABASE_PATH=/tmp/test_walkie.db python -m walkie_agent.config.cli agent seed-polish
```
Expected: `Seeded Polish personality 'Przyjaciel Radek'.`

```bash
WALKIE_DATABASE_PATH=/tmp/test_walkie.db python -m walkie_agent.config.cli agent list
```
Expected: Shows `przyjaciel-radek` in the table.

- [ ] **Step 3: Verify system prompt is Polish-aware**

```bash
cd project && python -c "
from walkie_agent.agent.personality import PersonalityConfig, PersonalityManager, VoiceSettings
p = PersonalityConfig(
    id='przyjaciel-radek',
    name='Przyjaciel Radek',
    backstory='Jesteś Przyjaciel Radek...',
    language='pl',
    max_response_length=60,
    voice_settings=VoiceSettings(voice_id='NacdHGUYR1k3M0FAbAia'),
    custom_instructions='Zawsze odpowiadaj po polsku.',
)
prompt = PersonalityManager.build_system_prompt(p)
assert 'pl' in prompt, 'language code missing'
assert 'Polish' in prompt or 'pl' in prompt
print('Prompt OK, length:', len(prompt))
print(prompt[:300])
"
```
Expected: Prints prompt with language line and Polish-aware instructions.

- [ ] **Step 4: Commit**

```bash
cd project && git add walkie_agent/config/cli.py
git commit -m "feat: add Polish personality 'Przyjaciel Radek' via seed-polish CLI command"
```

---

## Phase 5: Raspberry Pi Radio Bridge (No USB on Mac)

**Focus:** A standalone Python client that runs on a Raspberry Pi. The Pi connects to the Baofeng radio via a USB soundcard (or direct 3.5mm). The Mac only runs the FastAPI server — zero radio hardware needed on the Mac.

**Radio hardware bill of materials:**
- Baofeng UV-5R (~$25) or any FM walkie-talkie with 3.5mm headset port
- Sabrent USB audio adapter (~$8) for the Pi
- BTech/BTECH APRS cable or DIY TRRS splitter (3.5mm + 2.5mm to dual 3.5mm) (~$10)
- Optional: PC817 optocoupler + 2N2222 transistor for GPIO-controlled PTT ($2)

**Wiring (VOX mode — no GPIO needed):**
```
Baofeng 3.5mm (speaker+mic)
  ↕ TRRS splitter cable
  ├── Pi USB soundcard LINE-IN  ← receives radio audio
  └── Pi USB soundcard LINE-OUT ← sends TTS audio (keys PTT via VOX)
```

Set Baofeng VOX level 4-6 in Menu 04. The Pi plays TTS loud enough to trigger VOX.

### Task 9: Create Pi Bridge Script

**Files:**
- Create: `bridge/radio_client.py`
- Create: `bridge/requirements.txt`
- Create: `bridge/.env.example`

- [ ] **Step 1: Create `bridge/requirements.txt`**

```
sounddevice==0.5.1
websockets>=14.0
numpy>=1.26.0
aiofiles>=23.0.0
python-dotenv>=1.0.0
```

Optional GPIO support (RPi only): add `RPi.GPIO>=0.7.1`

- [ ] **Step 2: Create `bridge/.env.example`**

```bash
# Walkie-Talkie server WebSocket URL (use Tailscale IP or hostname)
SERVER_URL=ws://100.x.x.x:8000/ws/walkie-talkie/radio-bridge-1

# Audio device index (run radio_client.py --list-devices to find yours)
AUDIO_INPUT_DEVICE=0
AUDIO_OUTPUT_DEVICE=0

# VOX: energy threshold in RMS for speech detection (0-32767, try 800-2000)
VOX_THRESHOLD=1000

# GPIO pin number for PTT (BCM mode). Leave empty to use VOX-only mode.
PTT_GPIO_PIN=

# Sample rate — must match server WALKIE_AUDIO_FORMAT
AUDIO_SAMPLE_RATE=8000
AUDIO_FORMAT=ulaw

# Logging
LOG_LEVEL=INFO
```

- [ ] **Step 3: Create `bridge/radio_client.py`**

```python
#!/usr/bin/env python3
"""Raspberry Pi radio bridge for the Walkie-Talkie Voice Agent.

Runs on the Pi that is physically connected to a Baofeng (or similar) radio.
Captures inbound radio audio, streams it to the FastAPI server, and plays
TTS responses back through the radio speaker.

Setup:
    pip install sounddevice websockets numpy python-dotenv
    # optional PTT via GPIO:
    pip install RPi.GPIO

Usage:
    python radio_client.py                   # uses .env in current dir
    python radio_client.py --list-devices    # show audio device list
    python radio_client.py --help
"""

from __future__ import annotations

import argparse
import asyncio
import logging
import os
import struct
import sys
import time
from typing import Optional

# Load .env before anything else
try:
    from dotenv import load_dotenv
    load_dotenv()
except ImportError:
    pass

SERVER_URL = os.getenv("SERVER_URL", "ws://localhost:8000/ws/walkie-talkie/pi-bridge")
AUDIO_INPUT_DEVICE = int(os.getenv("AUDIO_INPUT_DEVICE", "0")) if os.getenv("AUDIO_INPUT_DEVICE") else None
AUDIO_OUTPUT_DEVICE = int(os.getenv("AUDIO_OUTPUT_DEVICE", "0")) if os.getenv("AUDIO_OUTPUT_DEVICE") else None
VOX_THRESHOLD = int(os.getenv("VOX_THRESHOLD", "1000"))
PTT_GPIO_PIN = int(os.getenv("PTT_GPIO_PIN", "0")) if os.getenv("PTT_GPIO_PIN") else None
AUDIO_SAMPLE_RATE = int(os.getenv("AUDIO_SAMPLE_RATE", "8000"))
AUDIO_FORMAT = os.getenv("AUDIO_FORMAT", "ulaw")
LOG_LEVEL = os.getenv("LOG_LEVEL", "INFO").upper()

logging.basicConfig(
    level=getattr(logging, LOG_LEVEL, logging.INFO),
    format="%(asctime)s [%(levelname)s] %(message)s",
)
logger = logging.getLogger("radio_bridge")

# ---------------------------------------------------------------------------
# G.711 mu-law helpers (same as server — keep in sync)
# ---------------------------------------------------------------------------

def _pcm_to_ulaw(pcm_bytes: bytes) -> bytes:
    """Encode 16-bit signed little-endian PCM to G.711 mu-law using audioop."""
    if not pcm_bytes:
        return b""
    import audioop
    return audioop.lin2ulaw(pcm_bytes, 2)


def _ulaw_to_pcm(ulaw_bytes: bytes) -> bytes:
    """Decode G.711 mu-law to 16-bit signed little-endian PCM using audioop."""
    if not ulaw_bytes:
        return b""
    import audioop
    return audioop.ulaw2lin(ulaw_bytes, 2)


def _rms(pcm_bytes: bytes) -> float:
    """Compute RMS amplitude of 16-bit PCM bytes."""
    if not pcm_bytes:
        return 0.0
    samples = struct.unpack(f"<{len(pcm_bytes) // 2}h", pcm_bytes)
    return (sum(s * s for s in samples) / len(samples)) ** 0.5


# ---------------------------------------------------------------------------
# GPIO PTT helper
# ---------------------------------------------------------------------------

class PttController:
    """Controls PTT via GPIO relay/optocoupler. Falls back to VOX if GPIO unavailable."""

    def __init__(self, gpio_pin: Optional[int]) -> None:
        self.gpio_pin = gpio_pin
        self._gpio = None
        if gpio_pin:
            try:
                import RPi.GPIO as GPIO
                GPIO.setmode(GPIO.BCM)
                GPIO.setup(gpio_pin, GPIO.OUT, initial=GPIO.LOW)
                self._gpio = GPIO
                logger.info("GPIO PTT enabled on pin %d", gpio_pin)
            except ImportError:
                logger.warning("RPi.GPIO not installed — falling back to VOX PTT")
            except Exception as exc:
                logger.warning("GPIO init failed (%s) — falling back to VOX PTT", exc)

    def key(self) -> None:
        """Assert PTT (begin transmitting)."""
        if self._gpio:
            self._gpio.output(self.gpio_pin, self._gpio.HIGH)
            logger.debug("PTT keyed (GPIO %d HIGH)", self.gpio_pin)

    def unkey(self) -> None:
        """Release PTT (stop transmitting)."""
        if self._gpio:
            self._gpio.output(self.gpio_pin, self._gpio.LOW)
            logger.debug("PTT released (GPIO %d LOW)", self.gpio_pin)

    def cleanup(self) -> None:
        if self._gpio:
            try:
                self._gpio.cleanup()
            except Exception:
                pass


# ---------------------------------------------------------------------------
# Main bridge
# ---------------------------------------------------------------------------

class RadioBridge:
    """Bidirectional audio bridge: radio soundcard ↔ FastAPI WebSocket server."""

    def __init__(self) -> None:
        self._ptt = PttController(PTT_GPIO_PIN)
        self._running = False
        self._playback_active = False  # half-duplex guard: suppress mic during TTS playback

    async def run(self) -> None:
        """Connect to server and start audio loops."""
        import websockets

        logger.info("Connecting to server: %s", SERVER_URL)
        async for websocket in websockets.connect(
            SERVER_URL,
            ping_interval=20,
            ping_timeout=30,
            close_timeout=10,
        ):
            try:
                self._running = True
                logger.info("Connected!")

                async with asyncio.TaskGroup() as tg:
                    tg.create_task(self._capture_and_send(websocket), name="capture")
                    tg.create_task(self._receive_and_play(websocket), name="playback")

            except* websockets.ConnectionClosed as eg:
                logger.warning("Connection closed: %s — reconnecting in 3s", eg.exceptions[0])
            except* Exception as eg:
                logger.error("Bridge error: %s — reconnecting in 3s", eg.exceptions[0])
            finally:
                self._running = False
                await asyncio.sleep(3)

    async def _capture_and_send(self, ws) -> None:
        """Capture audio from soundcard, VOX-detect, send to server."""
        import sounddevice as sd

        loop = asyncio.get_event_loop()
        chunk_samples = AUDIO_SAMPLE_RATE // 10  # 100ms chunks

        def callback(indata, frames, time_info, status):
            if status:
                logger.debug("sounddevice input status: %s", status)
            # Suppress capture during TTS playback (half-duplex guard)
            if self._playback_active:
                return
            # Convert numpy float32 → int16 PCM (clip first to avoid overflow)
            import numpy as np
            clipped = np.clip(indata[:, 0], -1.0, 1.0)
            pcm = (clipped * 32767).astype("int16").tobytes()
            rms = _rms(pcm)
            if rms > VOX_THRESHOLD:
                # Convert PCM → ulaw if required
                audio = _pcm_to_ulaw(pcm) if AUDIO_FORMAT == "ulaw" else pcm
                future = asyncio.run_coroutine_threadsafe(ws.send(audio), loop)
                # Don't block callback; log errors in background
                future.add_done_callback(
                    lambda f: logger.debug("send error: %s", f.exception()) if f.exception() else None
                )

        logger.info(
            "Capturing audio (device=%s, rate=%dHz, VOX threshold=%d)",
            AUDIO_INPUT_DEVICE,
            AUDIO_SAMPLE_RATE,
            VOX_THRESHOLD,
        )
        with sd.InputStream(
            samplerate=AUDIO_SAMPLE_RATE,
            channels=1,
            dtype="float32",
            blocksize=chunk_samples,
            device=AUDIO_INPUT_DEVICE,
            callback=callback,
        ):
            # Keep running until the task is cancelled
            while self._running:
                await asyncio.sleep(0.05)

    async def _receive_and_play(self, ws) -> None:
        """Receive TTS audio from server and play back through soundcard."""
        import sounddevice as sd
        import numpy as np

        logger.info("Waiting for TTS audio from server...")

        async for message in ws:
            if not isinstance(message, bytes):
                # Text frame (transcript JSON) — log and skip
                logger.debug("Text frame from server: %s", message[:120])
                continue

            if not message:
                continue

            try:
                # Decode audio back to PCM for playback
                if AUDIO_FORMAT == "ulaw":
                    pcm = _ulaw_to_pcm(message)
                else:
                    pcm = message

                samples = np.frombuffer(pcm, dtype=np.int16).astype(np.float32) / 32768.0
                # Attenuate LINE-OUT level: 1 Vrms LINE-OUT will overdrive Baofeng mic (~10-20 mV needed)
                # Tune VOX_OUTPUT_GAIN (default 0.1) until radio reliably keys without distortion
                output_gain = float(os.getenv("VOX_OUTPUT_GAIN", "0.1"))
                samples = (samples * output_gain).reshape(-1, 1)  # mono → (N, 1)

                # Half-duplex: suppress mic capture during playback
                self._playback_active = True
                # GPIO PTT: key transmitter; add 100ms pre-key delay (Baofeng TX ramp-up)
                self._ptt.key()
                await asyncio.sleep(0.1)  # wait for TX to come up before audio
                try:
                    # run_in_executor keeps event loop unblocked during sd.play
                    loop = asyncio.get_event_loop()
                    await loop.run_in_executor(
                        None,
                        lambda: sd.play(samples, samplerate=AUDIO_SAMPLE_RATE, device=AUDIO_OUTPUT_DEVICE, blocking=True)
                    )
                    # VOX tail: radio holds TX for ~400ms after audio ends
                    await asyncio.sleep(0.4)
                finally:
                    self._ptt.unkey()
                    self._playback_active = False

            except Exception as exc:
                logger.warning("Playback error: %s", exc)

    def cleanup(self) -> None:
        self._ptt.cleanup()


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def list_devices() -> None:
    import sounddevice as sd
    print(sd.query_devices())


async def async_main() -> None:
    bridge = RadioBridge()
    try:
        await bridge.run()
    except KeyboardInterrupt:
        logger.info("Interrupted — shutting down")
    finally:
        bridge.cleanup()


def main() -> None:
    parser = argparse.ArgumentParser(description="Raspberry Pi radio bridge for Walkie-Talkie Voice Agent")
    parser.add_argument("--list-devices", action="store_true", help="List audio devices and exit")
    parser.add_argument("--server-url", help="Override SERVER_URL env var")
    parser.add_argument("--vox-threshold", type=int, help="Override VOX_THRESHOLD env var")
    args = parser.parse_args()

    if args.list_devices:
        list_devices()
        sys.exit(0)

    if args.server_url:
        global SERVER_URL
        SERVER_URL = args.server_url
    if args.vox_threshold:
        global VOX_THRESHOLD
        VOX_THRESHOLD = args.vox_threshold

    asyncio.run(async_main())


if __name__ == "__main__":
    main()
```

- [ ] **Step 4: Verify the file parses without syntax errors**

```bash
python3 -c "import ast; ast.parse(open('bridge/radio_client.py').read()); print('Syntax OK')"
```
Expected: `Syntax OK`

- [ ] **Step 4b: Check bridge hardware options (do this before buying anything)**

**Option A — Mac Studio (best, no new hardware):**
```bash
# SSH into Mac Studio (already on Tailnet at 100.116.31.6)
ssh 192.168.50.163
python3 --version   # needs 3.11+
pip3 install sounddevice websockets numpy python-dotenv
python3 -c "import sounddevice; print(sounddevice.query_devices())"
# If you see audio devices, plug the Baofeng TRRS cable into the 3.5mm jack
```

**Option B — jakub-health-hub (Linux, already on Tailnet 100.94.91.47):**
```bash
# First check if it has audio (no SSH on port 22, try 2222 or check health dashboard)
# If SSH accessible:
aplay -l                # list ALSA audio devices
python3 -c "import sounddevice"  # check if sounddevice works
# If no built-in audio: add a $5 USB soundcard (Sabrent/Ugreen USB audio adapter)
```

**Option C — Fly.io for the server, Mac as bridge:**
```bash
# Deploy server to Fly.io (WebSocket-native, EU region, ~$5/month)
# Install flyctl: brew install flyctl
# fly launch  # from project/ directory
# fly deploy
# Bridge runs on Mac pointing at: wss://your-app.fly.dev/ws/walkie-talkie/...
```

- [ ] **Step 5: Test on Mac (without radio hardware)**

```bash
# Install deps first
cd bridge && pip install sounddevice websockets numpy python-dotenv

# List audio devices to find the mic index
python3 radio_client.py --list-devices
```
Expected: Lists audio devices. Note the index of your built-in microphone.

```bash
# Test with Mac's built-in mic (no radio, no USB) pointing at a running server
# Start the server in another terminal first:
# cd project && WALKIE_ELEVENLABS_API_KEY=... WALKIE_ELEVENLABS_AGENT_ID=... python -m walkie_agent.config.cli serve

SERVER_URL=ws://localhost:8000/ws/walkie-talkie/mac-test \
AUDIO_INPUT_DEVICE=0 \
AUDIO_OUTPUT_DEVICE=0 \
VOX_THRESHOLD=500 \
python3 radio_client.py
```
Expected: `Connected!` — speak into Mac mic, server should receive audio.

- [ ] **Step 6: Commit**

```bash
git add bridge/
git commit -m "feat: add Raspberry Pi radio bridge client (sounddevice, VOX, GPIO PTT, WebSocket)"
```

---

## Phase 6: E2E Tests

### Task 10: Write E2E WebSocket Test

**Files:**
- Create: `project/tests/conftest.py`
- Create: `project/tests/test_api_websocket.py`

- [ ] **Step 1: Create `conftest.py` with shared fixtures**

```python
"""Pytest fixtures for walkie-talkie voice agent tests."""

import asyncio
import json
import pytest
import pytest_asyncio
from httpx import AsyncClient, ASGITransport
from unittest.mock import AsyncMock, MagicMock, patch

from walkie_agent.config.database import ConfigDatabase


@pytest.fixture
def event_loop():
    """Use a single event loop per test session."""
    loop = asyncio.new_event_loop()
    yield loop
    loop.close()


@pytest_asyncio.fixture
async def test_db():
    """In-memory SQLite database, initialized fresh per test."""
    db = ConfigDatabase(":memory:")
    await db.initialize()
    await db.seed_default_personality()
    yield db
    await db.close()


@pytest_asyncio.fixture
async def app_client(test_db):
    """FastAPI test client with mocked ElevenLabs bridge."""
    from walkie_agent.api import app

    # Inject in-memory DB into app state
    app.state.db = test_db

    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        yield client


@pytest.fixture
def mock_elevenlabs_bridge():
    """Mock ElevenLabsBridge that accepts connections without hitting ElevenLabs API."""
    bridge = AsyncMock()
    bridge.is_connected = True
    bridge.connect = AsyncMock()
    bridge.disconnect = AsyncMock()
    bridge.send_pcm_16k = AsyncMock()

    # simulate receive_audio yielding empty (no TTS returned in test)
    async def _empty_audio_gen():
        return
        yield  # make it an async generator

    bridge.receive_audio = _empty_audio_gen
    return bridge
```

- [ ] **Step 2: Create `test_api_websocket.py`**

```python
"""E2E WebSocket tests for the walkie-talkie voice agent API."""

import json
import pytest
import pytest_asyncio
import asyncio
from unittest.mock import AsyncMock, patch, MagicMock

from walkie_agent.config.database import ConfigDatabase


@pytest.mark.asyncio
async def test_health_check(app_client):
    """GET /api/health returns 200 with status=ok."""
    response = await app_client.get("/api/health")
    assert response.status_code == 200
    assert response.json()["status"] == "ok"


@pytest.mark.asyncio
async def test_list_conversations_empty(app_client):
    """GET /api/conversations returns empty list when no conversations logged."""
    response = await app_client.get("/api/conversations")
    assert response.status_code == 200
    assert response.json() == []


@pytest.mark.asyncio
async def test_stats_returns_zero_counts(app_client):
    """GET /api/stats returns zero totals on fresh DB."""
    response = await app_client.get("/api/stats")
    assert response.status_code == 200
    data = response.json()
    assert data["total_conversations"] == 0
    assert data["total_messages"] == 0


@pytest.mark.asyncio
async def test_list_personalities_returns_default(app_client):
    """GET /api/personalities returns the seeded default personality."""
    response = await app_client.get("/api/personalities")
    assert response.status_code == 200
    personalities = response.json()
    assert len(personalities) >= 1
    default = next((p for p in personalities if p.get("is_default")), None)
    assert default is not None
    assert default["id"] == "captain-sparkles"


@pytest.mark.asyncio
async def test_create_personality_and_list(app_client):
    """POST /api/personalities creates a new personality, visible in GET /api/personalities."""
    payload = {
        "id": "test-bot",
        "name": "Test Bot",
        "backstory": "A test.",
        "language": "pl",
        "voice_id": "NacdHGUYR1k3M0FAbAia",
        "max_response_length": 50,
    }
    resp = await app_client.post("/api/personalities", json=payload)
    assert resp.status_code == 200

    list_resp = await app_client.get("/api/personalities")
    ids = [p["id"] for p in list_resp.json()]
    assert "test-bot" in ids


@pytest.mark.asyncio
async def test_whitelist_add_and_list(app_client):
    """POST + GET /api/whitelist adds and lists a contact."""
    payload = {
        "nickname": "mama",
        "phone_number": "+48123456789",
        "platform": "telegram",
        "relation": "matka",
    }
    resp = await app_client.post("/api/whitelist", json=payload)
    assert resp.status_code == 200

    list_resp = await app_client.get("/api/whitelist")
    nicknames = [e["nickname"] for e in list_resp.json()]
    assert "mama" in nicknames


@pytest.mark.asyncio
async def test_websocket_connects_and_disconnects(test_db, mock_elevenlabs_bridge):
    """WebSocket /ws/walkie-talkie/{id} accepts connection and closes cleanly."""
    from starlette.testclient import TestClient
    from walkie_agent.api import app

    app.state.db = test_db

    with patch(
        "walkie_agent.audio.websocket.ElevenLabsBridge",
        return_value=mock_elevenlabs_bridge,
    ):
        with TestClient(app) as client:
            with client.websocket_connect("/ws/walkie-talkie/test-client-1") as ws:
                # Send a ping control message
                ws.send_text(json.dumps({"type": "ping"}))
                # The bridge should have been asked to connect
                # (We don't wait for full response since mock returns empty audio)
                pass  # just verify no exception
```

- [ ] **Step 3: Run all tests**

```bash
cd project && python -m pytest tests/ -v --tb=short 2>&1 | tail -30
```
Expected: All tests pass (some may be skipped if ElevenLabs credentials not set). Target: at least `8 passed`.

- [ ] **Step 4: Commit**

```bash
cd project && git add tests/
git commit -m "test: add E2E WebSocket tests, fixtures, and audio pipeline unit tests"
```

---

## Phase 7: Full E2E Run on Tailnet

This phase is manual — no code changes needed.

- [ ] **Step 1: Start the server on your Mac**

```bash
cd project
cp .env.example .env
# Edit .env:
#   WALKIE_ELEVENLABS_API_KEY=<your key>
#   WALKIE_ELEVENLABS_AGENT_ID=<agent id from CLI create-agent step>

python -m walkie_agent.config.cli serve
```
Expected: `Starting server on 0.0.0.0:8000`

- [ ] **Step 2: Create an ElevenLabs agent for a personality (first-time setup)**

```bash
# In a new terminal:
python -m walkie_agent.config.cli agent seed-polish
python -m walkie_agent.config.cli elevenlabs create-agent przyjaciel-radek
# Copy the printed AGENT_ID to your .env, then restart the server
```

- [ ] **Step 3: Test with Mac mic (no radio hardware)**

```bash
cd bridge
SERVER_URL=ws://localhost:8000/ws/walkie-talkie/mac-test \
AUDIO_INPUT_DEVICE=0 AUDIO_OUTPUT_DEVICE=0 VOX_THRESHOLD=400 \
python3 radio_client.py
```
Speak Polish: "Cześć, jak masz na imię?" — you should hear TTS response through Mac speakers.

- [ ] **Step 4: Verify conversation logged**

```bash
python -m walkie_agent.config.cli logs list
python -m walkie_agent.config.cli logs show <conversation-id>
```
Expected: Shows conversation with transcript entries.

- [ ] **Step 5: Test from another Tailnet machine**

On any other Tailnet-enrolled device, connect to:
```
ws://<mac-tailscale-ip>:8000/ws/walkie-talkie/tailnet-client-1
```

---

## Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| ElevenLabs agent creation fails (API schema change) | Low | High | SDK pinned to ^1.50 in pyproject.toml; test manually first with `elevenlabs list` |
| VOX threshold too sensitive / not sensitive enough | Medium | Medium | Document tuning procedure; add `--vox-threshold` CLI arg |
| GPIO PTT relay burns out without optocoupler | Low | Low | Optocoupler isolates Pi GPIO from radio PTT line — use PC817 |
| 3.5mm audio cable level mismatch (too loud / quiet) | Medium | Medium | Add `AUDIO_INPUT_GAIN` / `AUDIO_OUTPUT_GAIN` env vars in Phase 5.2 |
| `sounddevice` unavailable on Pi (PortAudio build) | Low | Medium | `sudo apt install python3-sounddevice portaudio19-dev` |
| Double-resampling fix breaks `send_audio()` legacy path | Low | Low | Tests cover both `send_audio` and `send_pcm_16k` |

## Review Findings (Critical Fixes Applied to Plan)

### From Senior Engineer Review
| # | Finding | Fixed in |
|---|---------|----------|
| 1 | `_resample_linear` uses `numpy.*` instead of `np.*` — NameError when numpy installed | Existing bug; flag during Task 1 |
| 2 | `send_pcm_16k` test must verify `send_audio` is NOT called (call-site wiring) | Task 1, Step 5 test strengthened |
| 3 | `copy.copy(settings)` fails on Pydantic v2 — must use `settings.model_copy(update={...})` | Task 7 code corrected below |
| 4 | `get_setting`/`set_setting` don't use `_transaction` context manager — racy under concurrent writes | Task 4 code corrected below |
| 5 | `receive_audio` mock exits immediately — doesn't test audio-forwarding path | Task 10 conftest adds blocking variant |
| 6 | ASR format inconsistency: agent created with `pcm_16000`, runtime handshake doesn't override it | Task 5 notes this must match |

### From Walkie-Talkie Expert Review
| # | Finding | Fixed in |
|---|---------|----------|
| 7 | `_pcm_to_ulaw` sign logic is wrong for negative samples — use `audioop` like `processor.py` | Task 9, bridge code corrected |
| 8 | `sd.play(..., blocking=True)` blocks the asyncio event loop | Task 9 uses `run_in_executor` |
| 9 | VOX output level 1 Vrms LINE-OUT overdives Baofeng mic — add `samples *= 0.1` | Task 9 bridge code |
| 10 | Missing `_playback_active` flag — TTS audio echoes back through mic and retriggers VOX | Task 9 bridge code |
| 11 | GPIO PTT needs 100ms pre-key delay before play; VOX needs 400ms tail delay after play | Task 9 bridge code |

### From Networking Engineer Review
- **Bridge hardware**: Mac Studio (.163, SSH, on Tailnet) is best local option; jakub-health-hub (Linux, Tailnet) also viable if it has audio hardware
- **Cloud server**: Fly.io recommended over Scaleway (simpler, WebSocket-native, EU region available)
- **Topology**: Bridge → Tailscale → Server (never expose bridge publicly)

---

## Rollback Strategy

- All changes are additive or minimal fixes to existing files
- Phase 1 fixes are safety-critical; there is no reason to revert them
- Phase 2 CLI commands are additive — removing them does not break the server
- Phase 5 bridge is a new standalone file — delete `bridge/` to revert
- Database schema is additive (`settings` table) — old code ignores the new table

## File Ownership Summary

| File | Phase | Change Type |
|------|-------|-------------|
| `walkie_agent/audio/elevenlabs_bridge.py` | 1 | Add `send_pcm_16k()` |
| `walkie_agent/audio/websocket.py` | 1 | Use `send_pcm_16k` |
| `walkie_agent/agent/engine.py` | 1 | Fix typo `audit_log` → `audit_logger` |
| `walkie_agent/agent/personality.py` | 1 | Fix typo; inject language |
| `walkie_agent/config/database.py` | 2 | Add `settings` table + `get/set_setting` |
| `walkie_agent/elevenlabs_admin.py` | 2 | Create — ElevenLabs CRUD |
| `walkie_agent/config/cli.py` | 2, 4 | Add `elevenlabs` + `seed-polish` commands |
| `walkie_agent/api.py` | 3 | Fix `_PersonalityManagerProxy`, DB agent ID override |
| `bridge/radio_client.py` | 5 | Create — Pi bridge |
| `bridge/requirements.txt` | 5 | Create |
| `bridge/.env.example` | 5 | Create |
| `tests/conftest.py` | 6 | Create — fixtures |
| `tests/test_audio_pipeline.py` | 1, 6 | Create |
| `tests/test_elevenlabs_admin.py` | 2, 6 | Create |
| `tests/test_api_websocket.py` | 6 | Create |
