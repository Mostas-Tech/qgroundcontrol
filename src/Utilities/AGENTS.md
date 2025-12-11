---
description: 'Agent for shared utilities and helpers.'
tools: []
---
# Scope
- General helpers (file ops, coordinate math, logging, misc support classes) used across modules.
# Guidance
- Centralize reusable helpers here to avoid duplication; keep APIs backward compatible.
- Maintain thread safety and clear error reporting; document non-obvious behavior with brief comments.
- Use existing utility classes (`QGCFileDownload`, `ScreenTools`, etc.) before adding new ones.
# Checks
- Run utility-focused tests; smoke affected callers to ensure behavior remains unchanged.
