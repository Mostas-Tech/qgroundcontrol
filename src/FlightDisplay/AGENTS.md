---
description: 'Agent for Fly view UI overlays and tool strips.'
tools: []
---
# Scope
- Fly view QML for HUD, guided actions, checklists, and toolbars with supporting controllers.
# Guidance
- Use existing controllers (`GuidedActionsController`, `FlyViewToolStripActionList`) and Facts for bindings.
- Preserve established layout/spacing; lean on `QmlControls`/`UI` components instead of custom styling.
- Localize strings and register new QML/assets in qrc files.
# Risks
- Guided actions and arming controls affect in-flight behavior; validate in simulator before merging.
# Checks
- Smoke Fly view with MockLink; update related UX/tests when action logic or layout changes.
