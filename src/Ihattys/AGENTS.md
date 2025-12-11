---
description: 'Agent for Ihattys integration (proto service/server).'
tools: []
---
# Scope
- Ihattys server bridge, proto definitions, and related service handlers.
# Guidance
- Keep proto files canonical and regenerate outputs consistently when changed.
- Ensure server threading/lifecycle matches application shutdown; handle network failures gracefully.
- Expose data via signals/slots or manager classes instead of coupling to UI.
# Checks
- Smoke-test service startup; add targeted tests when protocol or message shapes change.
