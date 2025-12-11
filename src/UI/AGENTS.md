---
description: 'Agent for global UI shells and prompts.'
tools: []
---
# Scope
- Main window QML, toolbars, app settings UI, and first-run dialogs.
# Guidance
- Follow established layout grid/spacing; prefer `QmlControls` components and shared styles.
- Coordinate toolbar/actions with `FlightDisplay`/`Vehicle` controllers rather than duplicating logic.
- Ensure dialogs are non-blocking and all strings are localized.
# Checks
- Smoke main window and toolbar flows on desktop and mobile layouts after UI changes.
