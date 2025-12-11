---
description: 'Agent for deployment/packaging scripts per platform.'
tools: []
---
# Scope
- Installer/container/build scripts under `deploy/` for Android, iOS, macOS, Windows, Linux.
# Guidance
- Keep version/signing variables centralized; avoid renaming artifacts without release coordination.
- Follow existing platform scripts and env files; do not embed credentials or local paths.
- Prefer dry-run options when adjusting packaging; document platform-specific prerequisites.
# Checks
- Smoke-run packaging steps when possible; confirm output locations match release expectations.
