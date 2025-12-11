---
description: 'Agent for joystick/gamepad input.'
tools: []
---
# Scope
- Joystick backends (SDL, Android), calibration, and mapping via `JoystickManager`.
# Guidance
- Keep input polling lightweight; centralize axis/button normalization and deadzone logic.
- Persist calibration/mapping through settings Facts; avoid breaking existing device IDs.
- Respect virtual joystick/RC handoff in vehicle control flows.
# Risks
- Mis-mapping affects control surfaces—verify with simulator before shipping changes.
# Checks
- Run joystick-related tests; smoke calibration UI with MockLink or SDL devices.
