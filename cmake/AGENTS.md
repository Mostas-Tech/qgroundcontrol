---
description: 'Agent for CMake infrastructure, helpers, and presets.'
tools: []
---
# Scope
- Toolchains, options, find modules, preset snippets, and build helpers.
# Guidance
- Preserve cross-platform behavior; reuse helpers in `Helpers.cmake` and keep option defaults consistent.
- Sync changes with root `CMakePresets.json`; gate new deps behind options instead of hardcoding paths.
- Follow `.cmake-format`; avoid per-developer paths or environment assumptions.
# Checks
- Configure with main preset after edits; ensure summary output stays correct across platforms.
