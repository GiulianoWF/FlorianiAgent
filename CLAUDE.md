# floriani_agent — Multi-Agent LLM Proxy

Drop-in OpenAI-compatible proxy (`/v1/chat/completions` with SSE streaming) that sits between clients (like `cpp_voice_query`) and a llama.cpp backend, adding agentic capabilities: tool calling, sub-agent spawning, configurable pipelines, and persistent conversation history.

## Architecture

```
Client (cpp_voice_query, mock_client, curl)
  │
  │  POST /v1/chat/completions (OpenAI format, SSE streaming)
  ▼
HttpServer (cpp-httplib)
  │
  ▼
Pipeline (config-driven stages: add_history → llm_agent_loop → response)
  │
  ▼
Agent (agentic loop)
  │
  ├─► LlmClient ──► llama.cpp backend (localhost:8001)
  │       ▲               │
  │       │   SSE stream with delta.content / delta.tool_calls
  │       └───────────────┘
  │
  ├─► SkillExecutor (runs tools as host processes via popen)
  │
  ├─► spawn_agent (built-in tool: creates sub-agents with own context)
  │
  ├─► HistoryStore (SQLite3 persistence)
  │
  └─► Monitor (tracks agents, tool executions, internal messages)
```

## Source layout

```
src/
  main.cpp                      Entry point: parses CLI args, wires components, starts server

  core/                         Shared foundations
    types.h                     Message, ToolCall, SkillDef (with JSON serialization)
    config.h/.cpp               YAML config loading (yaml-cpp)

  inbound-server/               Inbound HTTP API
    http_server.h/.cpp          /v1/chat/completions, /health, /v1/monitor

  llm-link/                     Outbound LLM communication
    i_llm_client.h              Abstract interface (ILlmClient) for dependency injection
    llm_client.h/.cpp           Concrete implementation (libcurl SSE client)

  agent/                        Orchestration
    agent.h/.cpp                Agentic loop: LLM → tool calls → execute → repeat. Sub-agent spawning
    pipeline.h/.cpp             Config-driven stage engine with failure policies
    monitor.h/.cpp              Observability: active agents, tool executions, internal messages

  skills/                       Tool execution
    skill_executor.h            Abstract SkillExecutor interface (designed for future Docker support)
    host_skill_executor.h/.cpp  V1 implementation: fork/exec on host via popen()

  storage/                      Persistence
    i_history_store.h           Abstract interface (IHistoryStore) for dependency injection
    history_store.h/.cpp        Concrete SQLite implementation (conversations + messages tables)

test/
  mock_client.cpp               CLI client for testing: single query, interactive mode, or defaults
  agent_test.cpp                Unit tests for the agent loop (GMock-based, no external deps)

include/
  nlohmann/json.hpp             Vendored header-only JSON library
  httplib/httplib.h              Vendored cpp-httplib (HTTP server)
```

## Dependency injection & testing

The Agent class depends on abstract interfaces, not concrete implementations:

| Dependency | Interface | Concrete | Notes |
|------------|-----------|----------|-------|
| LLM client | `ILlmClient` | `LlmClient` (libcurl) | Agent uses `ILlmClient&` |
| Skill executor | `SkillExecutor` | `HostSkillExecutor` (popen) | Already abstract |
| History store | `IHistoryStore` | `HistoryStore` (SQLite) | Agent uses `IHistoryStore*` (optional) |
| Monitor | — | `Monitor` | Optional (`nullptr`-checked), no interface needed |

This allows unit testing the agent loop in isolation via GMock mocks (`MockLlmClient`, `MockSkillExecutor`, `MockHistoryStore`) without libcurl, SQLite, or any external service.

```bash
# Run agent unit tests (no external dependencies required)
bazel test //:agent_test
```

When adding new dependencies to the Agent, prefer injecting an interface rather than a concrete class. Keep interfaces minimal — only include methods that the Agent actually calls.

## How the agentic loop works

1. User message arrives via HTTP, goes through the pipeline, reaches the Agent
2. Agent sends messages + available tools to the LLM via `ILlmClient::chat()`
3. LlmClient parses the SSE stream:
   - `delta.content` tokens → accumulated as text, forwarded via `on_token` callback
   - `delta.tool_calls` → accumulated per-index (arguments arrive incrementally)
   - `finish_reason: "tool_calls"` → signals tool call completion
   - `finish_reason: "stop"` → signals final text response
4. If tool calls detected: Agent executes each via SkillExecutor, appends results as `role: "tool"` messages, loops back to step 2
5. If text response: Agent returns it, pipeline completes, HTTP server streams it to client
6. Built-in `spawn_agent` tool lets the LLM create sub-agents with independent contexts

## Tool calling protocol (Qwen3 / llama.cpp)

- Tools sent as OpenAI-format `tools` array in the API request
- llama.cpp with `--jinja` applies the Qwen3 chat template natively
- Tool results sent back as `{"role": "tool", "content": "...", "tool_call_id": "..."}`
- No custom parsing — the proxy relies entirely on llama.cpp's OpenAI-compatible output

## Runtime config and data

All lives in `~/.floriani-agent/` (configurable via `--config-dir`):

```
~/.floriani-agent/
  config.yaml        LLM endpoint, server port, system prompt, pipeline stages
  skills/            Skill definitions (JSON files)
  history.db         SQLite conversation history
  logs/              (reserved for future use)
```

### Skill definition format (`skills/*.json`)

```json
{
  "name": "shell_exec",
  "description": "Execute a shell command on the host and return its output",
  "parameters": {
    "type": "object",
    "properties": { "command": { "type": "string" } },
    "required": ["command"]
  },
  "executor": "/bin/bash -c",
  "execution_env": "host"
}
```

### Config format (`config.yaml`)

```yaml
llm:
  endpoint: "http://localhost:8001"
  timeout_seconds: 120
server:
  port: 8080
agent:
  system_prompt: "You are a helpful assistant."
  max_tool_iterations: 10
pipeline:
  stages:
    - name: add_history
      executor: builtin:add_history
      failure_policy: abort
    - name: llm_agent_loop
      executor: builtin:llm_agent_loop
      failure_policy: abort
    - name: response
      executor: builtin:response
      failure_policy: abort
```

## Build & run

```bash
# Build everything
bazel build //:floriani_agent //:mock_client //:agent_test

# Start the agent (requires llama.cpp running on :8001)
bazel-bin/floriani_agent

# Start with overrides
bazel-bin/floriani_agent --port 9000 --backend http://localhost:8001

# Test with mock client
bazel-bin/mock_client "What time is it?"
bazel-bin/mock_client -i                    # interactive mode
bazel-bin/mock_client --no-stream "Hello"   # non-streaming

# Run unit tests
bazel test //:agent_test

# Check monitoring endpoint
curl http://localhost:8080/v1/monitor | jq

# Regenerate compile_commands.json for IDE support
bazel run @wolfd_bazel_compile_commands//:generate_compile_commands
```

### CLI options (floriani_agent)

| Flag | Default | Description |
|------|---------|-------------|
| `--port PORT` | `8080` (or config) | Listen port |
| `--backend URL` | `http://localhost:8001` (or config) | LLM backend URL |
| `--config-dir DIR` | `~/.floriani-agent` | Config directory |
| `--new-conversation` | off | Start a fresh conversation |

## Dependencies

### BCR modules (see MODULE.bazel)
- `rules_cc` 0.2.17
- `sqlite3` 3.51.2.bcr.1
- `yaml-cpp` 0.9.0
- `wolfd_bazel_compile_commands` 0.5.2
- `googletest` 1.17.0.bcr.2 (unit testing + GMock)

### System packages
```bash
sudo apt install libcurl4-openssl-dev
```

### Vendored headers (in include/)
- nlohmann/json (copied from cpp_voice_query)
- cpp-httplib v0.18.7 (yhirose/cpp-httplib)

## HTTP endpoints

| Method | Path | Description |
|--------|------|-------------|
| POST | `/v1/chat/completions` | OpenAI-compatible chat (streaming + non-streaming) |
| GET | `/health` | Health check (`{"status":"ok"}`) |
| GET | `/v1/monitor` | Internal state: active agents, pipeline, tool executions |

## SQLite schema (history.db)

```sql
conversations(id TEXT PK, created_at TEXT)
messages(id INTEGER PK, conversation_id TEXT FK, role TEXT, content TEXT,
         tool_call_id TEXT, tool_calls_json TEXT, timestamp TEXT)
```

Last 20 messages are loaded into agent context on startup. All messages (user, assistant, tool calls, tool results) are persisted.

## Keeping this file up to date

When making changes to the codebase, update this CLAUDE.md to reflect them. This includes:
- New source files or directories added to the source layout
- New or changed interfaces/abstractions
- New dependencies added to MODULE.bazel
- New build targets, CLI flags, or HTTP endpoints
- Changes to the architecture diagram or agentic loop flow
