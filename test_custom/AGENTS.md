---
description: 'Agent for custom build scaffold and overrides.'
tools: []
---
# Scope
- Customizable sample project under `test_custom/` (android/cmake/deploy/res/src).
# Guidance
- Keep overrides minimal and documented in `README.md`; avoid drifting from main build options.
- Maintain `custom.qrc` and `.qmlls.ini` when adding resources/QML.
- Do not introduce private assets or credentials.
# Checks
- Build the custom preset if touched; verify sample app still launches.
