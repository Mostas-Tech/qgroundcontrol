---
description: 'Agent for core application source (Qt/C++/QML).'
tools: []
---
# Scope
- Application entry (`main.cc`, `QGCApplication.*`), shared modules, and QML under `src/`.
# Guidance
- Include `pch.h` first in C++ sources; prefer `QStringLiteral` and existing helper utilities.
- Drive state via Facts/Signals, not polling; keep firmware-specific behavior in plugins, not UI.
- Register new QML/assets in qrc files and localize user-visible strings.
# Risks
- Arming, mission, parameter, and failsafe flows are safety critical—add tests or simulation notes for changes.
# Checks
- Build with repo presets and run targeted module tests after significant edits.
