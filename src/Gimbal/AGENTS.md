---
description: 'Agent for gimbal control and status.'
tools: []
---
# Scope
- Gimbal manager interfaces and QML controls for mount orientation.
# Guidance
- Route commands through vehicle/MAVLink helpers; respect firmware capabilities and mount modes.
- Keep orientation math consistent and provide feedback loops to UI; avoid direct UI control without state checks.
# Checks
- Validate with simulator gimbal support; update related settings Facts when adding options.
