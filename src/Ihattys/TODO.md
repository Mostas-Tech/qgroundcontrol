# TODO for gRPC Server Communication (Android, No ESP32/RemoteID)

## Build System and Targets
- [ ] Fix library link name in `src/CMakeLists.txt` (link actual Ihattys target)
- [ ] Verify Android preset builds protobuf/grpc code
- [ ] Ensure generated files are added to Ihattys target
- [ ] Ensure logging compiles for "Ihattys" (Qt6::Core or replace qInfo)

## gRPC Service Definition and Codegen
- [ ] Keep proto minimal: `ihattys_info.proto` defines ProductService.GetProduct
- [ ] Confirm generated headers are included and importable

## Server Implementation
- [ ] Use async server scaffold in `IhattysServer.h`/`.cc`
- [ ] Decide bind address for Android (start with 127.0.0.1:50051)

## App Lifecycle Integration (Android)
- [ ] Create/hold single IhattysServer instance in app
- [ ] Wire start/stop in QGC app init/shutdown (Android build)

## Android Specifics
- [ ] Verify AndroidManifest has INTERNET permission
- [ ] If testing from host, use adb port forwarding

## Testing
- [ ] Add basic client smoke test (desktop/host) for ProductService.GetProduct
- [ ] Validate concurrency (multiple requests handled)

## Developer Ergonomics
- [ ] Add CMake option to toggle server (default ON for Android)
- [ ] Add runtime logging for server start/stop

## Build/Run
- [ ] Configure: use "android-ihattys-debug" preset
- [ ] Build/deploy to device/emulator
- [ ] Test with grpcurl via adb forward

## Future (Optional, Server-Only)
- [ ] Add TLS support (ServerCredentials)
- [ ] Extend proto with additional RPCs as needed
