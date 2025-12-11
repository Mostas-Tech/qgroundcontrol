---
description: 'Agent for developer tooling and setup scripts.'
tools: []
---
# Scope
- Utility scripts under `tools/` for setup/reference tasks.
# Guidance
- Keep scripts cross-platform where possible; avoid adding heavy dependencies.
- Document usage inline; ensure scripts fail loudly and avoid modifying user environment silently.
# Checks
- Smoke-run modified scripts; validate paths relative to repo root.
