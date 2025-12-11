---
description: 'Agent for terrain data and elevation queries.'
tools: []
---
# Scope
- Terrain tile download/cache, altitude queries, and provider integrations.
# Guidance
- Use existing cache/download managers; throttle network access and handle offline/failed tiles gracefully.
- Keep coordinate math consistent with mission planning; surface errors through signals for UI.
# Checks
- Run `test/Terrain`; validate planned altitudes and terrain displays in planner/fly views.
