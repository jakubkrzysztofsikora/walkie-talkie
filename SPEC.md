# SPEC.md — Walkie-Talkie Voice Agent System for Kids

## 1. Project Overview

A complete voice agent system that lets kids connect via walkie-talkie hardware, speak naturally to an AI-powered character, and receive spoken responses. The agent has configurable personality/backstory, maintains full conversation audit logs, and can use tools like web search, sandboxed code execution, and messaging to whitelisted numbers.

### Target Users
Illiterate children using walkie-talkie devices — all interaction is voice-based, no reading/writing required.

### Tech Stack
- **Backend**: Python 3.11, FastAPI, WebSockets
- **Voice**: ElevenLabs Conversational AI API (WebSocket) for STT + TTS + agent orchestration
- **LLM**: OpenAI GPT-4o (via ElevenLabs conv AI or direct)
- **Tools**: DuckDuckGo search, RestrictedPython sandbox, Twilio/telethon messaging adapters
- **Database**: SQLite (async aiosqlite)
- **Admin CLI**: Typer
- **Deployment**: Docker + docker-compose
- **Safety**: Input/content filtering, sandboxed execution, strict whitelist enforcement

---

## 2. System Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         WALKIE-TALKIE CLIENTS                               │
│  (Hardware: Raspberry Pi + radio HAT, or mobile app, or browser WebRTC)     │
│                        WebSocket audio streaming                            │
└────────────────────────────┬────────────────────────────────────────────────┘
                             │
┌────────────────────────────▼────────────────────────────────────────────────┐
│                         FASTAPI SERVER                                      │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐    │
│  │   Audio      │  │   Agent      │  │   Tool       │  │   Audit      │    │
│  │   Pipeline   │──│   Engine     │──│   Registry   │  │   Logger     │    │
│  │   Module     │  │   Module     │  │   Module     │  │   Module     │    │
│  └──────────────┘  └──────────────┘  └──────────────┘  └──────────────┘    │
│         │                  │                  │                │           │
│         └──────────────────┴──────────────────┘────────────────┘           │
│                                    │                                         │
│  ┌───────────────────────────────▼───────────────────────────────┐         │
│  │                     CONFIG DATABASE (SQLite)                   │         │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────────────┐ │         │
│  │  │   Agents     │  │  Whitelist   │  │   Conversations      │ │         │
│  │  │   Config     │  │   Entries    │  │   & Transcripts      │ │         │
│  │  └──────────────┘  └──────────────┘  └──────────────────────┘ │         │
│  └────────────────────────────────────────────────────────────────┘         │
└─────────────────────────────────────────────────────────────────────────────┘
                             │
┌────────────────────────────▼────────────────────────────────────────────────┐
│                         EXTERNAL SERVICES                                   │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐    │
│  │  ElevenLabs  │  │  OpenAI      │  │  DuckDuckGo  │  │  Twilio /    │    │
│  │  Conv AI     │  │  GPT-4o      │  │  Search      │  │  Telegram    │    │
│  └──────────────┘  └──────────────┘  └──────────────┘  └──────────────┘    │
└─────────────────────────────────────────────────────────────────────────────┘
                             │
┌────────────────────────────▼────────────────────────────────────────────────┐
│                         ADMIN CLI (Typer)                                   │
│  Local management tool for configuring agents, whitelist, reviewing logs    │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 3. Module Specifications

### 3.1 Audio Pipeline Module (`audio/`)

Handles WebSocket audio streaming between walkie-talkie clients and the voice agent.

#### Files
- `audio/websocket.py` — WebSocket endpoint manager
- `audio/processor.py` — Audio buffer, VAD (voice activity detection), format conversion
- `audio/elevenlabs_bridge.py` — Bridges our WebSocket to ElevenLabs Conversational AI WebSocket

#### Key Interfaces

```python
# audio/websocket.py
class AudioWebSocketManager:
    """Manages WebSocket connections from walkie-talkie clients."""
    
    async def handle_client(self, websocket: WebSocket, client_id: str):
        """
        Handle bidirectional audio streaming with a walkie-talkie client.
        
        Flow:
        1. Accept WebSocket connection
        2. Spawn ElevenLabs bridge for this client
        3. Forward audio chunks from client → ElevenLabs
        4. Forward audio responses from ElevenLabs → client
        5. On disconnect, cleanup and log conversation end
        """
        ...
    
    async def broadcast_to_client(self, client_id: str, audio_bytes: bytes):
        """Send synthesized audio back to a specific walkie-talkie client."""
        ...

# audio/processor.py  
class AudioBuffer:
    """Buffers incoming audio chunks, detects speech start/end."""
    
    def __init__(self, sample_rate: int = 16000, vad_aggressiveness: int = 2):
        self.sample_rate = sample_rate
        self.vad_aggressiveness = vad_aggressiveness
        self.buffer = bytearray()
        self.is_speaking = False
        self.silence_frames = 0
    
    def add_chunk(self, pcm_bytes: bytes) -> Optional[bytes]:
        """
        Add PCM audio chunk. Returns complete utterance when speech ends.
        Uses WebRTC VAD for speech detection.
        """
        ...
    
    def reset(self):
        """Clear buffer and reset state."""
        ...

class AudioFormatConverter:
    """Converts between audio formats."""
    
    @staticmethod
    def ulaw_to_pcm(ulaw_bytes: bytes) -> bytes:
        """Convert μ-law (common in walkie-talkies) to 16-bit PCM."""
        ...
    
    @staticmethod
    def pcm_to_ulaw(pcm_bytes: bytes) -> bytes:
        """Convert 16-bit PCM to μ-law."""
        ...
    
    @staticmethod
    def resample(pcm_bytes: bytes, from_rate: int, to_rate: int) -> bytes:
        """Resample PCM audio."""
        ...

# audio/elevenlabs_bridge.py
class ElevenLabsBridge:
    """
    Bridges a walkie-talkie client WebSocket to ElevenLabs Conversational AI.
    
    ElevenLabs Conv AI handles STT → LLM → TTS in one WebSocket.
    We inject custom system prompt (personality), tools, and audio format config.
    """
    
    def __init__(
        self,
        client_id: str,
        agent_config: AgentConfig,  # from config module
        audio_buffer: AudioBuffer,
        on_transcript: Callable[[str, str], Awaitable[None]],  # (client_id, text) -> None
        on_tool_call: Callable[[str, Any], Awaitable[Any]],  # (tool_name, params) -> result
    ):
        ...
    
    async def connect(self):
        """Connect to ElevenLabs Conv AI WebSocket."""
        ...
    
    async def send_audio(self, pcm_bytes: bytes):
        """Send audio chunk to ElevenLabs (they handle STT)."""
        ...
    
    async def receive_audio(self) -> AsyncIterator[bytes]:
        """Receive synthesized audio chunks from ElevenLabs TTS."""
        ...
    
    async def disconnect(self):
        """Clean disconnect from ElevenLabs."""
        ...
```

#### WebSocket Protocol

Client connects to: `ws://server/ws/walkie-talkie/{client_id}`

Message format (binary): Raw audio chunks (configurable format, default μ-law 8kHz)
Server sends back (binary): Synthesized audio in same format

Connection lifecycle:
1. Client opens WebSocket
2. Server creates AudioBuffer + ElevenLabsBridge for this client
3. Bidirectional streaming begins
4. On disconnect, server logs conversation end

---

### 3.2 Agent Engine Module (`agent/`)

The brain of the system — manages conversation state, personality injection, tool orchestration, and kid-friendly interaction patterns.

#### Files
- `agent/engine.py` — Main agent loop and conversation state machine
- `agent/personality.py` — Personality/backstory configuration and prompt builder
- `agent/kid_friendly.py` — Kid-specific safety filters and response enhancers
- `agent/tool_router.py` — Routes tool calls from the LLM to actual tool implementations

#### Key Interfaces

```python
# agent/personality.py
@dataclass
class PersonalityConfig:
    """Full personality configuration for a voice agent."""
    id: str  # unique agent ID
    name: str  # agent display name (e.g., "Captain Sparkles")
    backstory: str  # rich backstory text
    personality_traits: List[str]  # ["encouraging", "patient", "playful", "curious"]
    voice_settings: VoiceSettings
    language: str = "en"  # primary language
    max_response_length: int = 150  # keep responses short for kids
    allowed_tools: List[str] = field(default_factory=list)  # subset of tools this agent can use
    custom_instructions: str = ""  # extra behavior instructions

@dataclass  
class VoiceSettings:
    """Voice configuration for ElevenLabs."""
    voice_id: str  # ElevenLabs voice ID
    stability: float = 0.5
    similarity_boost: float = 0.75
    style: float = 0.3
    use_speaker_boost: bool = True

class PersonalityManager:
    """Loads and builds system prompts from personality configs."""
    
    def __init__(self, db: ConfigDatabase):
        self.db = db
    
    async def get_personality(self, agent_id: str) -> PersonalityConfig:
        """Load personality config from database."""
        ...
    
    def build_system_prompt(self, config: PersonalityConfig) -> str:
        """
        Build a complete system prompt from personality config.
        
        Includes:
        - Backstory and character definition
        - Personality trait instructions
        - Kid-friendly communication guidelines
        - Available tools description (if any)
        - Safety guardrails
        """
        ...
    
    async def list_personalities(self) -> List[PersonalityConfig]:
        """List all configured personalities."""
        ...
    
    async def save_personality(self, config: PersonalityConfig) -> None:
        """Save or update a personality config."""
        ...

# agent/engine.py
class ConversationState(Enum):
    IDLE = "idle"
    LISTENING = "listening"
    THINKING = "thinking"
    SPEAKING = "speaking"
    TOOL_CALL = "tool_call"
    ENDED = "ended"

class AgentEngine:
    """
    Manages a single conversation session.
    
    Uses ElevenLabs Conversational AI which handles STT→LLM→TTS internally.
    We configure it with our system prompt (personality) and tool definitions.
    """
    
    def __init__(
        self,
        client_id: str,
        personality: PersonalityConfig,
        tool_registry: ToolRegistry,
        audit_logger: AuditLogger,
    ):
        self.client_id = client_id
        self.personality = personality
        self.tool_registry = tool_registry
        self.audit_logger = audit_logger
        self.state = ConversationState.IDLE
        self.conversation_id = str(uuid.uuid4())
        self.start_time = datetime.utcnow()
    
    async def start_conversation(self) -> None:
        """Initialize conversation, log start."""
        ...
    
    async def handle_user_message(self, text: str) -> str:
        """
        Process transcribed user speech.
        
        ElevenLabs Conv AI handles the LLM call internally.
        If the LLM requests a tool call, we execute it and return the result
        so ElevenLabs can incorporate it into the response.
        """
        ...
    
    async def handle_tool_call(self, tool_name: str, params: dict) -> dict:
        """
        Execute a tool call requested by the LLM.
        Checks whitelist, runs tool, returns result.
        """
        ...
    
    async def end_conversation(self) -> None:
        """End conversation, finalize audit log."""
        ...

# agent/kid_friendly.py
class KidSafetyFilter:
    """
    Safety filters specifically for child interactions.
    
    - Content moderation on all inputs and outputs
    - Blocks inappropriate topics
    - Ensures simple vocabulary
    - Adds patience/encouragement to responses
    """
    
    BLOCKED_TOPICS = [
        "violence", "weapons", "drugs", "alcohol", 
        "adult content", "self-harm", "hate speech"
    ]
    
    @staticmethod
    def moderate_input(text: str) -> Tuple[bool, str]:
        """
        Check if user input is appropriate.
        Returns (is_safe, reason_or_none).
        """
        ...
    
    @staticmethod
    def enhance_response(response: str) -> str:
        """
        Ensure response is kid-friendly:
        - Simple vocabulary (grade 3-5 reading level equivalent when spoken)
        - Short sentences
        - Encouraging tone
        - No complex jargon
        """
        ...
    
    @staticmethod
    def simplify_text(text: str) -> str:
        """Simplify complex text for young children."""
        ...

# agent/tool_router.py
class ToolRegistry:
    """Registry of available tools the agent can use."""
    
    def __init__(self):
        self.tools: Dict[str, BaseTool] = {}
    
    def register(self, tool: BaseTool) -> None:
        """Register a tool."""
        ...
    
    def get_tool(self, name: str) -> Optional[BaseTool]:
        """Get a tool by name."""
        ...
    
    def get_openai_tools_schema(self) -> List[dict]:
        """
        Generate OpenAI function-calling schema for all registered tools.
        Used to configure ElevenLabs Conv AI tools.
        """
        ...

class BaseTool(ABC):
    """Base class for all agent tools."""
    
    name: str
    description: str
    parameters: dict  # JSON schema for parameters
    requires_whitelist: bool = False
    
    @abstractmethod
    async def execute(self, **params) -> dict:
        """Execute the tool. Returns JSON-serializable result."""
        ...
    
    def get_schema(self) -> dict:
        """Get OpenAI function schema for this tool."""
        ...
```

---

### 3.3 Tool System Module (`tools/`)

Implements the tools available to the agent.

#### Files
- `tools/search.py` — Web search tool
- `tools/sandbox.py` — Sandboxed Python script execution
- `tools/messaging.py` — Messaging tool (WhatsApp/Telegram/Signal) with whitelist
- `tools/base.py` — Base tool interface

#### Key Interfaces

```python
# tools/search.py
class WebSearchTool(BaseTool):
    """
    Search the web for information.
    Uses DuckDuckGo (no API key needed).
    
    Tool name: web_search
    """
    name = "web_search"
    description = "Search the internet for information to answer questions"
    parameters = {
        "type": "object",
        "properties": {
            "query": {
                "type": "string",
                "description": "The search query"
            }
        },
        "required": ["query"]
    }
    requires_whitelist = False
    
    async def execute(self, query: str) -> dict:
        """
        Execute web search.
        Returns: {"results": [{"title", "url", "snippet"}, ...]}
        """
        ...

# tools/sandbox.py
class SandboxTool(BaseTool):
    """
    Execute Python scripts in a restricted sandbox.
    
    Tool name: run_python
    Uses RestrictedPython + timeout + resource limits.
    """
    name = "run_python"
    description = "Run a Python calculation or simple script"
    parameters = {
        "type": "object",
        "properties": {
            "code": {
                "type": "string",
                "description": "Python code to execute"
            }
        },
        "required": ["code"]
    }
    requires_whitelist = False
    
    # Sandbox restrictions
    MAX_EXECUTION_TIME = 10  # seconds
    MAX_MEMORY_MB = 128
    ALLOWED_MODULES = ["math", "random", "datetime", "json", "statistics"]
    
    async def execute(self, code: str) -> dict:
        """
        Execute code in sandbox.
        Returns: {"success": bool, "output": str, "error": str|null}
        """
        ...

# tools/messaging.py
class MessagingTool(BaseTool):
    """
    Send text messages to whitelisted phone numbers.
    Supports WhatsApp (via Twilio), Telegram (via Bot API), Signal (via signal-cli).
    
    Tool name: send_message
    Strict whitelist enforcement — messages only to pre-approved numbers.
    """
    name = "send_message"
    description = "Send a text message to a parent or guardian"
    parameters = {
        "type": "object",
        "properties": {
            "recipient": {
                "type": "string",
                "description": "Nickname of the recipient (e.g., 'mom', 'dad')"
            },
            "message": {
                "type": "string",
                "description": "Message content to send"
            },
            "platform": {
                "type": "string",
                "enum": ["whatsapp", "telegram", "signal"],
                "description": "Messaging platform to use"
            }
        },
        "required": ["recipient", "message"]
    }
    requires_whitelist = True
    
    def __init__(self, whitelist: WhitelistManager, config: MessagingConfig):
        self.whitelist = whitelist
        self.config = config
        self.adapters = {
            "whatsapp": WhatsAppAdapter(config.twilio),
            "telegram": TelegramAdapter(config.telegram),
            "signal": SignalAdapter(config.signal),
        }
    
    async def execute(self, recipient: str, message: str, platform: str = "whatsapp") -> dict:
        """
        Send message to whitelisted recipient.
        Returns: {"success": bool, "message_id": str|null, "error": str|null}
        """
        # 1. Look up recipient in whitelist by nickname
        # 2. Verify platform is supported
        # 3. Send via appropriate adapter
        # 4. Return result
        ...

class WhitelistManager:
    """Manages the messaging whitelist."""
    
    def __init__(self, db: ConfigDatabase):
        self.db = db
    
    async def is_allowed(self, nickname: str) -> bool:
        """Check if a nickname is in the whitelist."""
        ...
    
    async def get_number(self, nickname: str) -> Optional[str]:
        """Get phone number for a whitelisted nickname."""
        ...
    
    async def add_entry(self, nickname: str, phone_number: str, platform: str, relation: str = "") -> None:
        """Add a whitelisted contact."""
        ...
    
    async def remove_entry(self, nickname: str) -> None:
        """Remove a whitelisted contact."""
        ...
    
    async def list_entries(self) -> List[WhitelistEntry]:
        """List all whitelisted contacts."""
        ...

@dataclass
class WhitelistEntry:
    nickname: str
    phone_number: str
    platform: str  # whatsapp/telegram/signal
    relation: str  # e.g., "mom", "dad", "grandma"
    created_at: datetime
```

---

### 3.4 Audit Logging Module (`audit/`)

Comprehensive logging of all conversations, transcripts, tool calls, and system events.

#### Files
- `audit/logger.py` — Main audit logger
- `audit/models.py` — Database models for audit data
- `audit/exporter.py` — Export functionality (JSON, CSV)

#### Key Interfaces

```python
# audit/logger.py
class AuditLogger:
    """
    Logs all conversation activity to SQLite.
    
    Each conversation creates:
    - One conversation record
    - Multiple transcript entries (user + agent turns)
    - Tool call records (if tools used)
    - Audio file references (if audio archival enabled)
    """
    
    def __init__(self, db: AuditDatabase):
        self.db = db
    
    async def log_conversation_start(
        self,
        conversation_id: str,
        client_id: str,
        agent_id: str,
        client_info: dict,
    ) -> None:
        """Log the start of a new conversation."""
        ...
    
    async def log_transcript(
        self,
        conversation_id: str,
        speaker: str,  # "user" | "agent"
        text: str,
        audio_reference: Optional[str] = None,
    ) -> None:
        """Log a transcript entry."""
        ...
    
    async def log_tool_call(
        self,
        conversation_id: str,
        tool_name: str,
        parameters: dict,
        result: dict,
        duration_ms: int,
        success: bool,
    ) -> None:
        """Log a tool call."""
        ...
    
    async def log_conversation_end(
        self,
        conversation_id: str,
        end_reason: str,  # "user_disconnect" | "timeout" | "error"
    ) -> None:
        """Log conversation end."""
        ...

# audit/models.py
# Database schema (SQLite tables):
#
# conversations:
#   id, client_id, agent_id, started_at, ended_at, end_reason, client_info_json
#
# transcripts:
#   id, conversation_id, speaker, text, audio_file, timestamp, sequence_num
#
# tool_calls:
#   id, conversation_id, tool_name, parameters_json, result_json, 
#   duration_ms, success, timestamp
#
# system_events:
#   id, event_type, details_json, timestamp

# audit/exporter.py
class AuditExporter:
    """Export audit logs in various formats."""
    
    async def export_conversation(self, conversation_id: str, format: str = "json") -> str:
        """
        Export a single conversation as JSON or CSV.
        Returns file path.
        """
        ...
    
    async def export_range(
        self,
        start: datetime,
        end: datetime,
        format: str = "json",
    ) -> str:
        """Export all conversations in a date range."""
        ...
    
    async def get_conversation_stats(
        self,
        conversation_id: str,
    ) -> ConversationStats:
        """Get statistics for a conversation."""
        ...

@dataclass
class ConversationStats:
    conversation_id: str
    duration_seconds: float
    total_turns: int
    user_turns: int
    agent_turns: int
    tool_calls: int
    avg_response_time_ms: float
```

---

### 3.5 Configuration & Admin CLI Module (`config/`)

Database models, configuration management, and the admin CLI tool.

#### Files
- `config/database.py` — SQLite database setup and queries (both config and audit)
- `config/models.py` — Pydantic models for all configuration
- `config/cli.py` — Typer-based admin CLI

#### Key Interfaces

```python
# config/database.py
class ConfigDatabase:
    """Async SQLite database for configuration and audit data."""
    
    def __init__(self, db_path: str = "data/walkie_agent.db"):
        self.db_path = db_path
    
    async def initialize(self):
        """Create all tables if they don't exist."""
        ...
    
    # Personality CRUD
    async def save_personality(self, config: PersonalityConfig) -> None: ...
    async def get_personality(self, agent_id: str) -> Optional[PersonalityConfig]: ...
    async def list_personalities(self) -> List[PersonalityConfig]: ...
    async def delete_personality(self, agent_id: str) -> None: ...
    
    # Whitelist CRUD
    async def add_whitelist_entry(self, entry: WhitelistEntry) -> None: ...
    async def get_whitelist_entry(self, nickname: str) -> Optional[WhitelistEntry]: ...
    async def list_whitelist_entries(self) -> List[WhitelistEntry]: ...
    async def remove_whitelist_entry(self, nickname: str) -> None: ...
    
    # Audit queries
    async def get_conversation(self, conversation_id: str) -> Optional[dict]: ...
    async def list_conversations(self, limit: int = 100, offset: int = 0) -> List[dict]: ...
    async def get_transcript(self, conversation_id: str) -> List[dict]: ...

# config/cli.py
# Typer CLI commands:
#
# walkie-agent agent create          # Create new agent personality (interactive)
# walkie-agent agent list            # List all agents
# walkie-agent agent show <id>       # Show agent details
# walkie-agent agent edit <id>       # Edit agent (interactive)
# walkie-agent agent delete <id>     # Delete agent
# walkie-agent agent set-default <id> # Set default agent
#
# walkie-agent whitelist add         # Add contact (interactive)
# walkie-agent whitelist list        # List whitelisted contacts
# walkie-agent whitelist remove <nickname>  # Remove contact
#
# walkie-agent logs list             # List recent conversations
# walkie-agent logs show <id>        # Show conversation transcript
# walkie-agent logs export <id>      # Export conversation
# walkie-agent logs stats            # Show usage statistics
#
# walkie-agent serve                 # Start the FastAPI server
# walkie-agent init                  # Initialize database
```

---

### 3.6 Main Application (`main.py`, `api.py`)

#### Files
- `api.py` — FastAPI application, WebSocket endpoint, REST API for admin
- `main.py` — Entry point, config loading, service startup
- `settings.py` — Pydantic Settings for env vars

#### Key Interfaces

```python
# settings.py
class Settings(BaseSettings):
    """Application configuration from environment variables."""
    
    # Server
    host: str = "0.0.0.0"
    port: int = 8000
    
    # ElevenLabs
    elevenlabs_api_key: str
    elevenlabs_agent_id: Optional[str] = None  # Conv AI agent ID
    
    # OpenAI (fallback / direct LLM usage)
    openai_api_key: Optional[str] = None
    
    # Database
    database_path: str = "data/walkie_agent.db"
    
    # Audio
    audio_sample_rate: int = 8000  # Hz (walkie-talkie typical)
    audio_format: str = "ulaw"  # "ulaw" | "pcm" | "alaw"
    vad_aggressiveness: int = 2
    
    # Messaging
    twilio_account_sid: Optional[str] = None
    twilio_auth_token: Optional[str] = None
    twilio_phone_number: Optional[str] = None
    telegram_bot_token: Optional[str] = None
    signal_phone_number: Optional[str] = None
    
    # Security
    admin_api_key: Optional[str] = None
    enable_audio_archival: bool = True
    
    class Config:
        env_prefix = "WALKIE_"
        env_file = ".env"

# api.py
app = FastAPI(title="Walkie-Talkie Voice Agent")

@app.websocket("/ws/walkie-talkie/{client_id}")
async def walkie_talkie_ws(websocket: WebSocket, client_id: str):
    """Main WebSocket endpoint for walkie-talkie clients."""
    ...

@app.get("/api/health")
async def health_check():
    """Health check endpoint."""
    ...

@app.get("/api/conversations")
async def list_conversations(limit: int = 100, offset: int = 0):
    """List conversations (admin API)."""
    ...

@app.get("/api/conversations/{conversation_id}")
async def get_conversation(conversation_id: str):
    """Get conversation details with transcript."""
    ...

@app.get("/api/stats")
async def get_stats():
    """Get system usage statistics."""
    ...
```

---

## 4. Data Flow

### 4.1 Conversation Flow (Typical)

```
1. Kid presses walkie-talkie PTT (push-to-talk) button
   → Walkie-talkie hardware transmits audio
   → Client device (Raspberry Pi/browser) opens WebSocket to server

2. Client streams audio chunks (μ-law, 8kHz) over WebSocket

3. Audio Pipeline:
   → Receive chunks → μ-law to PCM → resample to 16kHz → buffer
   → VAD detects speech start/end → package utterance

4. ElevenLabs Bridge:
   → Send utterance audio to ElevenLabs Conv AI WebSocket
   → ElevenLabs does: STT → LLM (with our personality prompt) → TTS
   → If LLM calls a tool, ElevenLabs sends tool_call event

5. Tool Execution (if needed):
   → Our server receives tool call request
   → Route to appropriate tool
   → Check whitelist for messaging tools
   → Execute tool → return result to ElevenLabs
   → ElevenLabs incorporates result into response

6. Response Audio:
   → ElevenLabs streams synthesized audio back
   → We convert PCM → μ-law, 8kHz
   → Stream back to client over WebSocket
   → Kid hears the agent's voice

7. All steps logged to audit database
```

### 4.2 Audio Format Pipeline

```
Walkie-Talkie (μ-law, 8kHz, mono)
    ↓ [ulaw_to_pcm]
PCM (16-bit, 8kHz, mono)
    ↓ [resample 8→16kHz]
PCM (16-bit, 16kHz, mono)  ← ElevenLabs expects this
    ↓ [ElevenLabs TTS output]
PCM (16-bit, 16kHz, mono)
    ↓ [resample 16→8kHz]
PCM (16-bit, 8kHz, mono)
    ↓ [pcm_to_ulaw]
Walkie-Talkie (μ-law, 8kHz, mono)
```

---

## 5. Database Schema

```sql
-- Agent personalities
CREATE TABLE IF NOT EXISTS personalities (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    backstory TEXT NOT NULL,
    personality_traits TEXT NOT NULL,  -- JSON array
    voice_id TEXT NOT NULL,
    voice_settings_json TEXT NOT NULL,
    language TEXT DEFAULT 'en',
    max_response_length INTEGER DEFAULT 150,
    allowed_tools TEXT,  -- JSON array, null = all
    custom_instructions TEXT,
    is_default BOOLEAN DEFAULT 0,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Messaging whitelist
CREATE TABLE IF NOT EXISTS whitelist (
    nickname TEXT PRIMARY KEY,
    phone_number TEXT NOT NULL,
    platform TEXT NOT NULL CHECK(platform IN ('whatsapp', 'telegram', 'signal')),
    relation TEXT,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Conversations
CREATE TABLE IF NOT EXISTS conversations (
    id TEXT PRIMARY KEY,
    client_id TEXT NOT NULL,
    agent_id TEXT NOT NULL,
    started_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    ended_at TIMESTAMP,
    end_reason TEXT,
    client_info_json TEXT,
    FOREIGN KEY (agent_id) REFERENCES personalities(id)
);

-- Transcripts
CREATE TABLE IF NOT EXISTS transcripts (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    conversation_id TEXT NOT NULL,
    speaker TEXT NOT NULL CHECK(speaker IN ('user', 'agent')),
    text TEXT NOT NULL,
    audio_file TEXT,
    sequence_num INTEGER NOT NULL,
    timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (conversation_id) REFERENCES conversations(id)
);

-- Tool calls
CREATE TABLE IF NOT EXISTS tool_calls (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    conversation_id TEXT NOT NULL,
    tool_name TEXT NOT NULL,
    parameters_json TEXT NOT NULL,
    result_json TEXT,
    duration_ms INTEGER,
    success BOOLEAN NOT NULL,
    timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (conversation_id) REFERENCES conversations(id)
);

-- System events
CREATE TABLE IF NOT EXISTS system_events (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    event_type TEXT NOT NULL,
    details_json TEXT,
    timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
```

---

## 6. Directory Structure

```
walkie-talkie-agent/
├── pyproject.toml              # Poetry deps, scripts entry point
├── .env.example                # Example environment variables
├── docker-compose.yml          # Docker deployment
├── Dockerfile                  # Container image
├── README.md                   # Documentation
├── data/                       # SQLite database (gitignored)
├── audio_archive/              # Archived audio files (gitignored)
│
├── walkie_agent/               # Main package
│   ├── __init__.py
│   ├── main.py                 # Entry point
│   ├── api.py                  # FastAPI app
│   ├── settings.py             # Pydantic settings
│   │
│   ├── audio/
│   │   ├── __init__.py
│   │   ├── websocket.py        # WebSocket endpoint manager
│   │   ├── processor.py        # Audio buffer, VAD, format conversion
│   │   └── elevenlabs_bridge.py # ElevenLabs Conv AI bridge
│   │
│   ├── agent/
│   │   ├── __init__.py
│   │   ├── engine.py           # Agent conversation loop
│   │   ├── personality.py      # Personality config & prompt builder
│   │   ├── kid_friendly.py     # Safety filters
│   │   └── tool_router.py      # Tool routing
│   │
│   ├── tools/
│   │   ├── __init__.py
│   │   ├── base.py             # Base tool interface
│   │   ├── search.py           # Web search
│   │   ├── sandbox.py          # Sandboxed Python execution
│   │   └── messaging.py        # Messaging with whitelist
│   │
│   ├── audit/
│   │   ├── __init__.py
│   │   ├── logger.py           # Audit logger
│   │   ├── models.py           # DB models
│   │   └── exporter.py         # Export functionality
│   │
│   └── config/
│       ├── __init__.py
│       ├── database.py         # Async SQLite database
│       ├── models.py           # Pydantic config models
│       └── cli.py              # Typer admin CLI
│
├── tests/
│   ├── __init__.py
│   ├── conftest.py             # Pytest fixtures
│   ├── test_audio.py
│   ├── test_agent.py
│   ├── test_tools.py
│   ├── test_audit.py
│   └── test_api.py
│
└── scripts/
    └── setup_radio.sh          # Raspberry Pi radio HAT setup
```

---

## 7. Environment Variables

```env
# Required
WALKIE_ELEVENLABS_API_KEY=sk_...

# Optional
WALKIE_OPENAI_API_KEY=sk-...
WALKIE_ELEVENLABS_AGENT_ID=...
WALKIE_DATABASE_PATH=data/walkie_agent.db
WALKIE_AUDIO_SAMPLE_RATE=8000
WALKIE_AUDIO_FORMAT=ulaw
WALKIE_VAD_AGGRESSIVENESS=2
WALKIE_TWILIO_ACCOUNT_SID=...
WALKIE_TWILIO_AUTH_TOKEN=...
WALKIE_TWILIO_PHONE_NUMBER=+1...
WALKIE_TELEGRAM_BOT_TOKEN=...
WALKIE_SIGNAL_PHONE_NUMBER=+1...
WALKIE_ADMIN_API_KEY=secret
WALKIE_ENABLE_AUDIO_ARCHIVAL=true
```

---

## 8. API Endpoints

| Method | Path | Description |
|--------|------|-------------|
| WS | `/ws/walkie-talkie/{client_id}` | Walkie-talkie audio streaming |
| GET | `/api/health` | Health check |
| GET | `/api/conversations` | List conversations |
| GET | `/api/conversations/{id}` | Get conversation + transcript |
| GET | `/api/stats` | System statistics |
| GET | `/api/export/{id}` | Export conversation |

---

## 9. Safety & Guardrails

1. **Content Moderation**: All inputs and outputs filtered through kid-safety checks
2. **Messaging Whitelist**: Strict — messages only to pre-approved contacts
3. **Sandbox Restrictions**: No network, no file system, limited imports, timeout, memory limit
4. **Audio Archival**: Optional but recommended for monitoring
5. **No PII Storage**: Client IDs are opaque, no personal data stored
6. **Rate Limiting**: Per-client rate limits on tool usage
7. **Conversation Timeout**: Auto-end after N minutes of inactivity
