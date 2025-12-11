---
description: 'Agent for video streaming and recording.'
tools: []
---
# Scope
- Video manager, receiver backend, subtitle writer, and settings integration.
# Guidance
- Keep pipeline selection aligned with platform (GStreamer/QtMultimedia); reuse `VideoReceiver`.
- Avoid blocking UI on stream startup; surface errors via signals and settings Facts.
- Preserve recording paths and subtitle formats; mind threading between decoder and UI.
# Checks
- Smoke video playback/recording with simulator or RTSP source; run related tests if modified.
