---
description: 'Agent for GPS/RTK support.'
tools: []
---
# Scope
- GPS provider, RTK injection, RTCM handling, and GPS Fact groups.
# Guidance
- Keep RTCM handling in `GPSProvider`/`RTCMMavlink`; ensure thread safety for serial/network I/O.
- Use `GPSRTKFactGroup` for UI bindings and maintain `GPSRTKFact.json` metadata accuracy.
- Keep message definitions (`definitions.h`, `sensor_*`) aligned with MAVLink schemas.
# Checks
- Run `test/GPS`; verify RTK status propagates to UI/logs in simulator.
