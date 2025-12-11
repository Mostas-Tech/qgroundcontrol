---
description: 'Agent for UTM/SP integration and UI.'
tools: []
---
# Scope
- UTM services, network interfaces, authorization, and associated QML panels/visuals.
# Guidance
- Keep REST/service interactions resilient with retries and clear logging; externalize endpoints/config.
- Respect vehicle state when issuing UTM commands; ensure QML bindings remain reactive.
- Register new UI assets in qrc and localize strings.
# Risks
- Authorization/network failures can block flight; add tests or logging breadcrumbs for new flows.
# Checks
- Smoke UI components with mock data; run targeted tests when protocols change.
