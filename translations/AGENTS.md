---
description: 'Agent for translations and lupdate helpers.'
tools: []
---
# Scope
- Qt `.ts` catalogs and scripts (`qgc-lupdate*`) managing translations.
# Guidance
- Avoid manual edits to generated `.ts`; use provided lupdate scripts and keep `qgc.ts`/`qgc-json.ts` in sync.
- Add strings with `qsTr`/`qsTrId` and context comments; register new locales in qrc if added.
- Keep translation tools path-safe and Python-compatible; avoid bloating repos with unused locales.
# Checks
- Run translation update scripts after UI string changes; ensure qrc references remain valid.
