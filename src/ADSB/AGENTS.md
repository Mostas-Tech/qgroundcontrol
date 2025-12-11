---
description: 'Agent for ADS-B ingestion and visualization.'
tools: []
---
# Scope
- ADS-B TCP link handling, vehicle parsing, and integration with map overlays.
# Guidance
- Keep networking in `ADSBTCPLink` and lifecycle in `ADSBVehicleManager`; avoid UI updates off the main thread.
- Normalize positions/altitudes via `QGeoCoordinate` helpers and respect update throttling.
- Use existing telemetry conversion helpers; avoid duplicating vehicle identity logic.
# Risks
- Threading between socket callbacks and UI, and vehicle ID collisions when merging tracks.
# Checks
- Run ADS-B tests under `test/ADSB`; smoke map/list displays for connect/disconnect flows.
