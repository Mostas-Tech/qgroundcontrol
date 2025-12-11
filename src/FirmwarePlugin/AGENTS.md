---
description: 'Agent for firmware capability adapters.'
tools: []
---
# Scope
- Base firmware plugin APIs, factories, and PX4/APM implementations.
# Guidance
- Keep capability flags authoritative; avoid UI-specific logic in plugins.
- Route mission/RTL/geofence behavior through overrides; align command availability with firmware parameter schema.
- Update factories/managers when adding plugins; keep mission command definitions consistent with MAVLink.
# Risks
- Impacts arming, failsafe, and mode handling—simulate and add tests for behavioral changes.
# Checks
- Run plugin-related tests; exercise mode/command availability with MockLink for each firmware.
