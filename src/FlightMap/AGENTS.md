---
description: 'Agent for map rendering, overlays, and airspace.'
tools: []
---
# Scope
- Map QML, mission/traffic/geofence overlays, and airspace layers.
# Guidance
- Use `FlightMap` components and helper models rather than manipulating map items directly.
- Keep coordinate/altitude math consistent (`QGCGeo` helpers) and throttle updates for performance.
- Register map assets in qrc; localize layer titles/tooltips.
# Checks
- Validate mission planning and map cache flows; run overlay-related tests when logic changes.
