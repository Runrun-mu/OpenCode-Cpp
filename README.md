<div align="center">

# OpenCodeCpp

### AI Coding Agent in C++17

**A cross-platform AI coding assistant with TUI, multi-LLM support, and a full tool system — built entirely by [NYAI](https://github.com/Runrun-mu/NYAI-Idea-Is-All-You-Need).**

[![C++17](https://img.shields.io/badge/C++-17-blue?style=flat-square&logo=cplusplus)](https://isocpp.org)
[![CMake](https://img.shields.io/badge/build-CMake-064F8C?style=flat-square&logo=cmake)](https://cmake.org)
[![FTXUI](https://img.shields.io/badge/TUI-FTXUI_v5-4fc08d?style=flat-square)](https://github.com/ArthurSonzogni/FTXUI)
[![Tests](https://img.shields.io/badge/tests-17%2F17_passing-brightgreen?style=flat-square)]()
[![License](https://img.shields.io/badge/license-MIT-green?style=flat-square)](LICENSE)

</div>

---

## What is OpenCodeCpp?

OpenCodeCpp is a terminal-based AI coding assistant written in C++17 — similar to Claude Code or Codex CLI, but as a native binary. It connects to OpenAI or Anthropic APIs and provides a full agent loop with tool calling, streaming, and session management.

**Why C++?** Cross-platform, small binary, no runtime dependency. Agents should run everywhere — desktop, server, embedded.

```
> Write a snake game in HTML

AI: I'll create a snake game for you...
[Tool: file_write] ✓ Created snake.html
[Tool: bash] ✓ Opened in browser

Done! The snake game is ready at ./snake.html
```

---

## Features

| Category | Features |
|:---------|:---------|
| **TUI** | FTXUI fullscreen interface, scrollable chat, Markdown rendering (code blocks, headings, bold, lists) |
| **LLM** | OpenAI (GPT-4o etc.) + Anthropic (Claude) with SSE streaming |
| **Tools** | bash, read, write, edit, glob, grep, ls, web_search, web_fetch, skill, subagent |
| **Session** | SQLite persistence, resume sessions with `--session <id>` |
| **Compact** | Codex-style 3-layer context compression (system + recent 20k tokens + summary) |
| **Plan Mode** | `/plan` for read-only analysis, `/execute` to run the plan |
| **Steer** | Type guidance while AI is generating — it adjusts on the fly |
| **Subagent** | AI can spawn child agents for parallel subtasks |
| **Skills** | SKILL.md knowledge packages with YAML frontmatter, auto-activated by file glob |
| **Codex Auth** | OAuth device code flow for ChatGPT login (no API key needed) |
| **Parallel** | Multiple tool calls executed concurrently via `std::async` |

---

## Quick Start

### Prerequisites

- CMake 3.14+
- C++17 compiler (Clang, GCC, MSVC)
- libcurl
- SQLite3

### Build

```bash
git clone https://github.com/Runrun-mu/OpenCode-Cpp.git
cd OpenCode-Cpp
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

FTXUI and nlohmann/json are fetched automatically by CMake.

### Run

```bash
# With OpenAI
export OPENAI_API_KEY="sk-..."
./opencode --provider openai --model gpt-4o

# With Anthropic
export ANTHROPIC_API_KEY="sk-ant-..."
./opencode --provider anthropic --model claude-sonnet-4-20250514

# With Codex Auth (ChatGPT login, no API key)
./opencode --provider openai --model gpt-4o --auth codex

# Resume a session
./opencode --session <session-id>
```

### Run Tests

```bash
cd build
ctest --output-on-failure
# 17/17 test suites, all passing
```

---

## Slash Commands

Type `/` to see auto-suggestions. Available commands:

| Command | Description |
|:--------|:------------|
| `/compact [instructions]` | Compress conversation history (Codex-style 3-layer) |
| `/status` | Show model, tokens, cost, session info |
| `/plan <prompt>` | Analyze codebase with read-only tools, output implementation plan |
| `/execute` | Execute the last plan with full tool access |
| `/skill [name\|list]` | List or activate SKILL.md knowledge packages |
| `/clear` | Clear chat display |
| `/help` | Show all commands |

---

## Tools

| Tool | Description |
|:-----|:------------|
| `bash` | Execute shell commands |
| `file_read` | Read file contents |
| `file_write` | Write/create files |
| `file_edit` | Edit files with string replacement |
| `glob` | Find files by pattern |
| `grep` | Search file contents with regex |
| `ls` | List directory contents |
| `web_search` | Search the web (DuckDuckGo) |
| `web_fetch` | Fetch and extract web page content |
| `skill` | Load SKILL.md knowledge packages |
| `subagent` | Spawn child agent for subtasks |

---

## Context Compaction

Aligned with Codex CLI's design philosophy:

1. **System messages** — never compressed, always preserved
2. **Recent messages** — last ~20,000 tokens kept verbatim
3. **Summary** — older history summarized via LLM into a handoff document

Auto-triggers when tokens exceed 80% of context window or 50+ messages.

Manual: `/compact` or `/compact focus on the TODO items`

---

## Skill System

Create `~/.opencode/skills/python.md`:

```markdown
---
name: python-expert
description: Python programming expert
globs:
  - "*.py"
  - "**/*.py"
---

# Python Expert

You are a Python expert. Prefer list comprehensions...
```

Skills auto-activate based on file globs in the working directory.

---

## Project Structure

```
src/
├── main.cpp              # Entry point
├── config/               # JSON config + CLI args
├── llm/                  # LLM providers
│   ├── provider.h        # Abstract interface
│   ├── openai.cpp        # OpenAI + streaming
│   ├── anthropic.cpp     # Anthropic + streaming
│   ├── streaming.cpp     # SSE parser
│   └── agent_loop.cpp    # Tool calling loop + compact
├── tui/                  # FTXUI interface
│   ├── tui.cpp           # Main TUI + event handling
│   ├── render.cpp        # Message + status bar rendering
│   ├── markdown.cpp      # Streaming Markdown renderer
│   └── input.cpp         # Input history
├── tools/                # 11 tools
│   ├── bash.cpp          # Shell execution
│   ├── file_read.cpp     # File reading
│   ├── file_write.cpp    # File writing
│   ├── file_edit.cpp     # String replacement editing
│   ├── glob.cpp          # File pattern matching
│   ├── grep.cpp          # Content search
│   ├── ls.cpp            # Directory listing
│   ├── web_search.cpp    # Web search (DuckDuckGo)
│   ├── web_fetch.cpp     # Web content fetching
│   └── subagent.cpp      # Child agent spawning
├── skills/               # Skill system
├── session/              # SQLite persistence
├── auth/                 # Codex OAuth auth
└── utils/                # HTTP client, helpers
tests/                    # 17 test suites
```

---

## Configuration

Config file: `~/.opencode/config.json`

```json
{
  "default_model": "gpt-4o",
  "default_provider": "openai",
  "context_window": 128000,
  "compact_threshold": 102400,
  "max_tokens": 4096,
  "api_keys": {
    "openai": "sk-...",
    "anthropic": "sk-ant-..."
  }
}

```

Environment variables override config: `OPENAI_API_KEY`, `ANTHROPIC_API_KEY`.

---

## Built by NYAI

This entire project — 8000+ lines of C++, 17 test suites, 11 tools — was autonomously built by [NYAI](https://github.com/Runrun-mu/NYAI-Idea-Is-All-You-Need), a 6-agent AI orchestration engine, across multiple sprints in under 24 hours.

---

## License

MIT
