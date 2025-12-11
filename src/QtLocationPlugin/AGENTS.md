---
description: 'Agent for custom QtLocation provider glue.'
tools: []
---
# Scope
- Map provider integration and QtLocation plugin customizations.
# Guidance
- Keep provider keys/config externalized; avoid hardcoding service tokens or URLs.
- Maintain compatibility with targeted QtLocation API versions and platform differences.
# Checks
- Build a map-enabled preset; verify provider initialization and tile fetch in simulator.
