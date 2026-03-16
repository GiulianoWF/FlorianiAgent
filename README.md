# Floriani Agent

A multi-agent LLM proxy written in C++ that adds agentic capabilities to any llama.cpp backend: tool calling, sub-agent spawning, configurable pipelines, and persistent conversation history.

It exposes an OpenAI-compatible API (`/v1/chat/completions` with SSE streaming), so existing clients work without modification.

## How it works

```
Client (curl, custom app, etc.)
  |
  |  POST /v1/chat/completions
  v
Floriani Agent (proxy)
  |
  |-- Pipeline stages (configurable)
  |-- Agent loop: LLM -> tool calls -> execute -> repeat
  |-- Sub-agent spawning (dynamic delegation)
  |-- Conversation history (SQLite)
  |
  v
llama.cpp backend (localhost:8001)
```

1. A client sends a standard chat completion request
2. The agent loop forwards it to the LLM, which may request tool calls
3. Tools are executed locally, results fed back to the LLM
4. The loop repeats until the LLM produces a final text response
5. The response is streamed back to the client via SSE

The LLM can also spawn **sub-agents** with independent roles and contexts for complex tasks (research, validation, planning). Only the main agent's response reaches the client.

## Prerequisites

- **Bazel** 9+
- **libcurl** development headers
- A **llama.cpp** server running with `--jinja` (for native tool calling)

```bash
# Ubuntu/Debian
sudo apt install libcurl4-openssl-dev

# Start llama.cpp (example with Qwen3-32B)
llama-server -m Qwen3-32B-Q4_K_M.gguf --port 8001 --jinja
```

## Build

```bash
bazel build //:floriani_agent //:mock_client
```

## Run

```bash
# Start the agent (connects to llama.cpp on localhost:8001)
bazel-bin/floriani_agent

# With overrides
bazel-bin/floriani_agent --port 9000 --backend http://localhost:8001

# Start a fresh conversation (ignores history)
bazel-bin/floriani_agent --new-conversation
```

### Quick test

```bash
# Interactive mode
bazel-bin/mock_client -i

# Single query
bazel-bin/mock_client "What time is it?"

# Non-streaming
bazel-bin/mock_client --no-stream "Hello"

# Direct curl
curl -s http://localhost:8080/v1/chat/completions \
  -H "Content-Type: application/json" \
  -d '{"messages":[{"role":"user","content":"Hello"}],"stream":true}'
```

## Test

```bash
bazel test //:agent_test
```

Unit tests use GMock to test the agent loop in isolation, with no external dependencies (no LLM server, no SQLite, no network).

## Configuration

All config and data lives in `~/.floriani-agent/` (override with `--config-dir`):

```
~/.floriani-agent/
  config.yaml        # LLM endpoint, server port, system prompt, pipeline stages
  skills/            # Skill definitions (JSON files)
  history.db         # SQLite conversation history
```

### config.yaml

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

### Adding skills

Create a JSON file in `~/.floriani-agent/skills/`:

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

The LLM will see this tool and can call it during the agentic loop. Results are fed back automatically.

## API

| Method | Path | Description |
|--------|------|-------------|
| POST | `/v1/chat/completions` | OpenAI-compatible chat (streaming + non-streaming) |
| GET | `/health` | Health check |
| GET | `/v1/monitor` | Internal state: active agents, tool executions, pipeline status |

## Architecture

```
src/
  main.cpp                  Entry point
  core/                     Types, config loading
  inbound-server/           HTTP server (cpp-httplib)
  llm-link/                 LLM client with SSE parsing (libcurl)
  agent/                    Agent loop, pipeline, monitor
  skills/                   Skill execution (abstract interface + host implementation)
  storage/                  Conversation history (SQLite)
test/
  agent_test.cpp            Unit tests (GMock)
  mock_client.cpp           CLI test client
```

Key dependencies are injected via abstract interfaces (`ILlmClient`, `SkillExecutor`, `IHistoryStore`), making each component testable in isolation.

## License

TBD
