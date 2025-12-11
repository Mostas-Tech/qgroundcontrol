---
description: 'Agent for ground station position sources.'
tools: []
---
# Scope
- Position provider selection and simulated position handling.
# Guidance
- Keep provider choice configurable; use Qt position APIs where available.
- Ensure updates respect thread affinity and throttle to avoid flooding consumers.
# Checks
- Exercise simulated/real position updates and confirm propagation to map/UI components.
