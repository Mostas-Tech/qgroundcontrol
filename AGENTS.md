---
description: 'High-signal agent for developing and maintaining QGroundControl (Qt/C++/QML MAVLink GCS).'
tools: []
---
# Mission
- Build the safest, fastest changes to QGroundControl with clear intent and minimal blast radius.
- Stay aligned with existing patterns; avoid surprise dependencies or UX drift.
- Keep companion projects (AgriBoard firmware + ArduCopter Lua bridge) in sync with QGC UI/features.

# Repo map (top)
- Core: `src/` (app). Entrypoint `src/main.cc`, app wrapper `src/QGCApplication.*`, global pch `src/pch.h`.
- Modules: `FlightDisplay`, `FlightMap`, `MissionManager`, `Vehicle`, `AutoPilotPlugins`, `FirmwarePlugin`, `FactSystem`, `Settings`, `Comms`, `MAVLink`, `Camera`, `VideoManager`, `Terrain`, `Utilities`, `QmlControls`, `UI`, `Viewer3D`, `AnalyzeView`, `UTMSP`.
- Platform: `android/`, `QtLocationPlugin/`, `deploy/`, `tools/`, `cmake/`.
- Assets: `resources/`, `qgc*.qrc`, `qgroundcontrol.qrc`, `translations/`.
- Tests: `test/`, `test_custom/`.
- Config: `CMakeLists.txt`, `CMakePresets.json`, `.clang-format`, `.clang-tidy`, `.editorconfig`, `.qmlls.ini`.
- Companion firmware repo: `E:\Github\AgriBoard_v1` (ESP32/PlatformIO) for agricultural payload control.
- ArduCopter Lua bridge: `E:\Github\AgriBoard_v1\lua\agriboard.lua` for mission command/param sync.

# Build/test recipe
- Configure: `cmake --preset <preset>` from repo root.
- Build: `cmake --build --preset <build-preset>`.
- CTest: run via configured presets; align with `test` targets.
- QML: follow `.qmlls.ini`; keep bindings; avoid JS loops when signals work.
- C++: honor `.clang-format`/`.clang-tidy`; prefer `QStringLiteral`, `auto` only when obvious.

# Coding patterns
- Use FactSystem for settings/params; reuse metadata; avoid hardcoded enums if a Fact exists.
- Route vehicle-specific logic through `AutoPilotPlugins`/`FirmwarePlugin`; UI should query capabilities, not assume.
- Mission items via `MissionManager`; terrain/geo via `Terrain`; telemetry via `Comms`/`MAVLink`.
- Video surfaces through `VideoManager`; map widgets through `FlightMap` and `QmlControls`.
- Keep resources registered in the right `.qrc`; localize UI strings and ensure `translations/` coverage when appropriate.

# UX guardrails
- Preserve established layouts in `FlightDisplay` and `FlightMap`; prefer composable controls from `QmlControls`/`UI`.
- Avoid blocking UI work on threads; leverage signals/slots and bindings.
- Be conservative with animations; align with existing QGC visual language.

# Risk rules
- Ask/flag before: large refactors, protocol changes, dependency additions, asset deletions.
- Safety-critical areas: arming/disarming, mission upload/download, geofence/rally, failsafe logic, RC/joystick, parameter write. Add tests or logging breadcrumbs.
- Keep changes reversible; add focused comments only where intent is non-obvious.

# Workflow habits
- Search with `rg`; inspect with `cmake --build` not ad-hoc compiler calls.
- Check nearby files for patterns before introducing new ones.
- When touching QML/C++, ensure matching updates to resources and any needed preset/config wiring.
- Cross-verify interface changes with `AgriBoard_v1` (PlatformIO root `E:\Github\AgriBoard_v1`) and the ArduCopter Lua trigger script so commands/params stay aligned.

# Quick module cues
- `FlightDisplay/`: in-flight UI (e.g., `FlyViewToolStripActionList.qml`), HUD, overlays.
- `FlightMap/`: map visuals, layers, airspace.
- `MissionManager/`: mission model, transactions, fencing/rally handling.
- `Vehicle/`: vehicle state, modes, sensors, link state.
- `AutoPilotPlugins/` & `FirmwarePlugin/`: firmware-specific capabilities and param glue.
- `FactSystem/`: typed access to params/settings; do not bypass.
- `Settings/`: app-level prefs; respect defaults.
- `Comms/` & `MAVLink/`: link management, message plumbing.
- `VideoManager/`: video capture/stream display.
- `QmlControls/`, `UI/`: reusable controls; prefer using over bespoke widgets.

# Checklist before submit
- Build preset passes; no warnings introduced if avoidable.
- Tests updated/added for logic changes; manual test notes for UI.
- Resources added to `.qrc`; strings localized when user-facing.
- Changelog or docs touched if user-visible change; keep commits scoped.
- Document any AgriBoard protocol change + ensure Lua bridge expectations remain valid.
