---
description: 'Agent for GitHub workflows, templates, and automation.'
tools: []
---
# Scope
- GitHub Actions, issue/PR templates, and repo metadata under `.github/`.
- CI matrices for supported Qt/OS toolchains and release automation.
# Guidance
- Pin Actions to SHAs, avoid secrets in logs, and keep cache keys stable across jobs.
- Prefer shared/composite steps over duplicated YAML; align workflows with release/security docs.
- Keep templates/labels consistent with contribution guidelines; avoid surprise workflow triggers.
# Checks
- Sanity-check YAML with `act` or `--dry-run` where possible; ensure new workflows have timeouts and permissions set.
