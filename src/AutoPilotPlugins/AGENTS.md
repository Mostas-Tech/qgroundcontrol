---
description: 'Agent for firmware-specific autopilot plugins (PX4/APM).'
tools: []
---
# Scope
- Plugin classes for firmware setup, parameter metadata, and capability exposure.
# Guidance
- Route firmware differences through plugin overrides, not UI conditionals; rely on `Fact` metadata for settings.
- Respect parameter write ordering and safety gates; use `FirmwarePlugin` APIs for command availability.
- Keep setup QML registered in qrc and localized; avoid duplicating mission command definitions.
# Risks
- Arming, mission, and geofence behaviors are safety critical—require tests or simulation notes for changes.
# Checks
- Run `test/AutoPilotPlugins`; validate setup flows with MockLink or simulator per firmware.
