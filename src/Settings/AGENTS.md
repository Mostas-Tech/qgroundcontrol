---
description: 'Agent for application settings.'
tools: []
---
# Scope
- Settings Facts, persistence, and settings UI components.
# Guidance
- Define settings via `SettingsGroup`/`SettingsFact`; avoid ad-hoc QSettings access.
- Keep defaults/meta JSON aligned with UI expectations and migrations; surface settings through `UI/AppSettings`.
- Consider platform overrides (mobile/desktop) when changing defaults.
# Checks
- Run settings-related tests; verify new settings appear, localize correctly, and persist across restarts.
