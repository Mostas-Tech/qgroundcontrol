---
description: 'Agent for vehicle model, state, and control glue.'
tools: []
---
# Scope
- Core `Vehicle` class, mode/arming logic, parameter handling, and vehicle-facing UI glue.
# Guidance
- Route firmware-specific behavior through plugins; avoid embedding firmware assumptions directly.
- Keep threading clear: vehicle objects on main thread, link callbacks via queued signals.
- Use Facts for parameters/state and preserve request/update sequencing.
# Risks
- Arming, failsafe, and mode transitions are safety critical—require tests or simulator validation.
# Checks
- Run vehicle-related tests; validate connect/arm/mission flows with MockLink.
