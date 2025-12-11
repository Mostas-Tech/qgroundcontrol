---
description: 'Agent for mission planning, upload, and download.'
tools: []
---
# Scope
- Mission item models, transaction logic, fence/rally handling, and mission controller QML glue.
# Guidance
- Use `MissionController`/`MissionManager` queues; avoid bypassing transaction/state tracking.
- Honor firmware capabilities via `FirmwarePlugin` when exposing commands/modes.
- Keep coordinate frames/altitudes and command packing aligned with MAVLink specifications.
# Risks
- Mission upload/download and fence changes are safety critical—add tests and simulate changes.
# Checks
- Run `test/MissionManager`; validate plan upload/download with MockLink.
