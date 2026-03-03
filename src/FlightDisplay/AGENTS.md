---
description: 'Agent for FlightDisplay module layout and in-flight presentation wiring.'
tools: []
---
# Scope
- `src/FlightDisplay/` ownership and any future files for in-flight display composition.
- Coordination points with `src/FlyView/` (HUD/video/tooling) and `src/FlightMap/` (map overlays).

# Current layout note
- This directory is intentionally minimal in the current tree; most active Fly UI logic lives in `FlyView`.
- If adding new FlightDisplay code, keep responsibility boundaries clear: display composition here, mission/map models in their existing modules.

# Guidance
- Reuse existing Fly controllers and Fact bindings instead of creating parallel state paths.
- Keep UI updates signal-driven and avoid frame-loop polling or blocking work on the UI thread.
- Register new QML/assets in the correct `.qrc` and localize user-facing text.

# Risks
- In-flight display changes can affect pilot situational awareness; treat layout, arming controls, and failsafe visibility as safety-critical.

# Checks
- Build with configured CMake presets and smoke-test Fly view (video, HUD, toolstrip, map overlays) using simulator/MockLink.
