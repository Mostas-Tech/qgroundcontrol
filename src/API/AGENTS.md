---
description: 'Agent for plugin API surface (core plugin, options, component info).'
tools: []
---
# Scope
- `QGCCorePlugin`, `QGCOptions`, and QML component registration points.
# Guidance
- Preserve compatibility for external plugins; gate new hooks with defaults that mimic stock behavior.
- Use `QmlComponentInfo` to expose QML overrides instead of branching UI code.
- Document new plugin points and keep option defaults aligned with mainline QGC UX.
# Checks
- Build without custom plugins to ensure defaults still load; exercise UI entry points when adding hooks.
