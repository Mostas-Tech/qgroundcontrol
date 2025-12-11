---
description: 'Agent for shared assets and Qt resource packaging.'
tools: []
---
# Scope
- Icons, fonts, audio, firmware, calibration assets, and other resources referenced via qrc.
# Guidance
- Update `qgc*.qrc`/`qgroundcontrol.qrc` when adding/removing assets; keep filenames stable for QML.
- Prefer SVG/vector where possible; compress binaries and document licenses for third-party assets.
- Keep platform-specific resources in their subfolders to avoid bloat.
# Checks
- Ensure builds pick up updated qrc entries; smoke affected UI to confirm asset paths.
