---
description: 'Agent for core test suites.'
tools: []
---
# Scope
- QtTest/QML unit tests organized per module under `test/`.
# Guidance
- Use existing test harnesses (`MockLink`, `UnitTest`) and avoid network/file system flakiness.
- Keep tests deterministic (no sleeps when signals suffice) and mirror module structure.
- Add lightweight fixtures/data; register new tests in `CMakeLists.txt`.
# Checks
- Run `ctest --preset <build-preset>` or module tests after changes; ensure coverage for new code paths.
