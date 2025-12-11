---
description: 'Agent for Analyze view tools (logs, console, inspector, geotagging).'
tools: []
---
# Scope
- Analyze view QML and controllers for log download, MAVLink console/inspector, and geotagging.
# Guidance
- Reuse controllers (`LogDownloadController`, `MAVLinkInspectorController`, `GeoTagController`) instead of new flows.
- Keep heavy parsing in workers; surface progress through signals to avoid blocking the UI thread.
- Extend log parsers (ULog/PX4Log) conservatively and keep tests/data in sync; localize new strings and register assets in qrc.
# Checks
- Run module tests in `test/AnalyzeView`; smoke log download/console with MockLink or sample logs.
