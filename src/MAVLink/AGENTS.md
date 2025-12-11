---
description: 'Agent for MAVLink protocol helpers.'
tools: []
---
# Scope
- MAVLink signing, FTP/image protocols, and bundled MAVLink library code.
# Guidance
- Treat generated MAVLink headers as upstream; avoid direct edits and prefer updating the generator/source repo.
- Keep protocol helpers compliant with MAVLink specs, preserving retry/backoff semantics.
- Align event library updates (`LibEvents`) with firmware expectations.
# Checks
- Run MAVLink tests; verify compatibility with MockLink and existing log files.
