---
description: 'Agent for Facts, metadata, and Fact-based UI.'
tools: []
---
# Scope
- Core `Fact` classes, `FactGroup`, metadata definitions, and Fact UI controls.
# Guidance
- Prefer Facts over raw QVariant access; keep `FactMetaData` defaults/units aligned with settings and parameters.
- Facts must live on owning object threads; emit value changes via signals rather than polling.
- Update `FactControls` QML when adding new control types; localize labels/help text.
# Checks
- Run `test/FactSystem`; verify new Facts surface correctly in parameter manager and settings.
