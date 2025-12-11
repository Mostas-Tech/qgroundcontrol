---
description: 'Agent for Android platform glue (JNI, serial helpers).'
tools: []
---
# Scope
- Android-specific interfaces (`AndroidInterface`, `AndroidSerial`) and JNI bridges used at runtime.
# Guidance
- Guard platform calls with `#ifdef __android__`; keep QObject ownership on the Qt thread.
- Coordinate API changes with the Gradle project under `android/`; avoid hardcoded permissions or paths.
- Keep serial detection stable and match Java/Kotlin signatures when JNI changes are needed.
# Checks
- Build the Android preset; verify serial discovery and interface initialization on device/emulator.
