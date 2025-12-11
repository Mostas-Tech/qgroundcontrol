---
description: 'Agent for camera control, metadata, and stream info.'
tools: []
---
# Scope
- Camera control plumbing (`VehicleCameraControl`, `MavlinkCameraControl`), metadata, and simulated camera logic.
# Guidance
- Drive capabilities from `CameraMetaData`/camera definitions; use existing MAVLink command helpers.
- Keep simulated and real camera code paths consistent; avoid redundant timers or blocking calls.
- Register new assets in qrc and localize UI text.
# Checks
- Run `test/Camera`; smoke camera UI/streams in simulator to confirm state updates and command responses.
