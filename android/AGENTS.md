---
description: 'Agent for Android Gradle project and packaging.'
tools: []
---
# Scope
- AndroidManifest, Gradle build files, and `res/src` content for the mobile build.
# Guidance
- Use `./gradlew`/`gradlew.bat`; keep SDK/NDK/toolchain versions aligned with CMake presets.
- Keep manifest permissions minimal; preserve packageId/signing configs and Qt deployment settings.
- Place assets under `res/`; avoid large binaries and hardcoded paths.
# Checks
- Run `./gradlew assembleDebug` (or matching preset) when feasible; review merged manifest for permission changes.
