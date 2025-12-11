---
description: 'Agent for Follow Me positioning and commands.'
tools: []
---
# Scope
- Follow Me position providers and vehicle follow command handling.
# Guidance
- Wrap platform/location providers here; bound update rates and validate coordinates before use.
- Use `Vehicle`/`FirmwarePlugin` helpers for follow commands and capability checks.
- Keep UI bindings reactive and localized; avoid blocking operations on the UI thread.
# Risks
- Incorrect position handling can drive vehicles unexpectedly—simulate before enabling changes.
# Checks
- Run `test/FollowMe`; smoke Follow Me flows with MockLink or location simulator.
