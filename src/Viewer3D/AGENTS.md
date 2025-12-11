---
description: 'Agent for 3D viewer and terrain visualization.'
tools: []
---
# Scope
- 3D map rendering, tile parsing, shaders, and QML bridge components.
# Guidance
- Keep geometry generation efficient using existing terrain/tile helpers; avoid blocking render threads.
- Handle tile downloads asynchronously with caching; ensure OpenGL profile compatibility.
- Register shaders/assets in qrc and keep memory use bounded.
# Checks
- Smoke 3D view on supported platforms; monitor memory and frame time when loading tiles.
