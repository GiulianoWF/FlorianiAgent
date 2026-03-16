# Floriani Agent

A multi-agent system written in C++ that sits between clients (like `cpp_voice_query`) and a local LLM server, adding agentic capabilities: tool use, multi-agent coordination, configurable processing pipelines, and persistent conversation history.

## 1. Core Identity

Floriani Agent is a **drop-in proxy** that exposes an OpenAI-compatible API (`/v1/chat/completions` with SSE streaming). Clients like `cpp_voice_query` talk to it exactly as they would talk to a bare LLM server — no client-side changes needed. Internally, the agent intercepts requests, runs agentic loops (tool calls, sub-agent coordination, pipeline stages), and streams back only the final text response.

**Default LLM backend:** Qwen3-32B (`Qwen_Qwen3-32B-Q4_K_M.gguf`) served by llama.cpp at `localhost:8001` (configurable).

## 2. Multi-Agent Architecture

The system is a **multi-agent runtime**, not a single-agent wrapper.

- **Main agent** receives each user request and decides how to handle it.
- **Sub-agents** are **spawned dynamically** by the main agent at runtime based on the task. Each sub-agent has its own role, system prompt, skills, and conversation context.
- Examples of dynamic sub-agents: a research agent to gather information, a validator agent to check a response, a planner agent to break down a complex task.
- Agents communicate via **internal messages** — structured messages passed between agents that are never exposed to the external client. Only the main agent produces the final response streamed to the client.

### Agent Lifecycle
1. Main agent receives a user message via the API.
2. Main agent decides whether to handle it directly or spawn sub-agents.
3. Sub-agents execute their tasks, exchange internal messages, and report back.
4. Main agent synthesizes the final response and streams it to the client.

## 3. Skills (Tool Calling)

Skills are **tools the LLM can invoke** during its agentic loop, following the tool-call pattern (like OpenAI function calling).

### Skill Definition
Each skill is defined as a file in the skills directory with:
- **Name** — unique identifier.
- **Description** — natural language description (injected into LLM context).
- **Parameter schema** — JSON schema defining the inputs.
- **Executor** — a command or script to run when the skill is called.

### Skill Execution
- The LLM's **native tool-call format** is used (Qwen3 supports function calling in its chat template). The agent relies on llama.cpp to handle the formatting — no custom parsing.
- When the LLM emits a tool call, the agent executes the corresponding skill, feeds the result back into the LLM context, and continues the agentic loop until the LLM produces a final text response.

### Execution Environment
- **V1:** Skills run as local processes on the host.
- **Future:** The skill execution interface must be abstract enough to support running skills in Docker containers. Each skill's config will declare its execution environment (host or container). The abstraction should be designed now, Docker implementation comes later.

## 4. Config-Driven Processing Pipeline

Each request flows through a **configurable pipeline** of stages, defined in a config file (YAML or JSON).

### Default Pipeline
```
user_message → add_history → llm_agent_loop → response
```

### Expanded Pipeline (example)
```
user_message → add_history → llm_agent_loop → sandbox_check → stats_tracker → response
```

### Pipeline Stage Definition
Each stage in the config specifies:
- **Name** — identifier for logging and monitoring.
- **Executor** — what runs this stage (built-in module, external program, or sub-agent).
- **Failure policy** — what happens if this stage fails:
  - `retry_with_feedback` — feed the error back to the LLM as context and re-run from the LLM stage.
  - `skip` — ignore the failure and continue to the next stage.
  - `abort` — stop the pipeline and report the error to the client.
  - `fallback` — route to an alternative stage.

### Future Pipeline Use Cases
- Sandbox validation of tool calls before execution.
- Response quality checks.
- Statistics and usage tracking.
- Parallel execution of alternative strategies for comparison.

## 5. Conversation History

- Stored in **SQLite3**.
- Tracks all messages: user inputs, agent responses, internal agent-to-agent messages, tool calls and results.
- History is loaded into agent context as needed (e.g., the last N messages for the main agent, relevant context for sub-agents).

## 6. Monitoring Endpoint

A dedicated HTTP endpoint exposes the **internal state** of the system for observability.

### V1: Raw Log Access
- Returns structured JSON with:
  - Active sub-agents (count, roles, goals).
  - Internal message log (agent-to-agent communication).
  - Current pipeline stage for each active request.
  - Plans in execution and their progress.
  - Recent skill executions and results.

### Future: Interpreted Monitoring
- A separate LLM instance interprets the raw logs and provides natural-language summaries of what the system is doing and why.

## 7. Configuration

All config and data lives in `$HOME/.floriani-agent/` (path configurable).

```
$HOME/.floriani-agent/
  config.yaml          # Main config: LLM endpoint, pipeline definition, agent defaults
  skills/              # Skill definitions
  history.db           # SQLite conversation history
  logs/                # Internal logs
```

## 8. Technical Constraints

- **Language:** C++
- **Build system:** Bazel
- **LLM protocol:** OpenAI-compatible chat completions API (both inbound from clients and outbound to the LLM backend)
- **Tool calling:** Native model format via llama.cpp (no custom parsing)
- **Database:** SQLite3 for history
- **Streaming:** SSE for client-facing responses
