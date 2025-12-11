---
description: 'Agent for link management and MAVLink transport.'
tools: []
---
# Scope
- Serial/TCP/UDP/Bluetooth links, log replay, `MAVLinkProtocol`, and `LinkManager`.
# Guidance
- Maintain thread affinity: link I/O on link threads, UI updates via queued signals.
- Use `LinkConfiguration` factories and `LinkManager` for creation; keep MAVLink signing/sequence handling intact.
- Treat log replay timing carefully; prefer existing replay controllers over ad-hoc timers.
# Risks
- Link reconnect behavior and packet framing are user-visible; avoid regressions in error handling.
# Checks
- Run `test/Comms`; smoke connect/disconnect and log replay with MockLink.
