---
description: 'Agent for reusable QML controls.'
tools: []
---
# Scope
- Shared QML components (inputs, popups, indicators) used across the app.
# Guidance
- Extend existing controls before adding new ones; maintain property bindings and styling conventions.
- Register new controls/assets in qrc and localize strings with `qsTr`/`qsTrId`.
- Avoid heavy JS logic—prefer signals/bindings and Fact-based data sources.
# Checks
- Smoke affected controls on desktop/mobile form factors; update docs/examples if APIs change.
