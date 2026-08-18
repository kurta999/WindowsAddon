# ADR-0001: Headless libraries and a composition root

- Status: Accepted
- Date: 2026-08-12

## Context

WindowsAddon began as one wxWidgets executable. Protocol rules, persistence,
automation, operating-system calls, and widget behavior consequently shared
dependencies and lifetimes. Hardware-free tests either required GUI state or
could not exercise production code directly.

## Decision

Keep protocol, persistence, automation, and platform behavior in separate
headless CMake targets. Treat `MyApp::OnInit()` as the composition root that
selects concrete adapters and connects them to the wxWidgets layer through
narrow interfaces in `src/interface`.

The native Visual Studio project builds the same production source files; it
does not define an alternative architecture.

## Consequences

- Core behavior builds and runs on CI without wxWidgets or physical hardware.
- Tests can provide direct fakes for clocks, transports, filesystems, and event
  sinks.
- Some legacy GUI-edge services still use `CSingleton`/`wxGetApp()` and must be
  migrated when touched.
- Adding a production source requires updating both CMake and the maintained
  Visual Studio project, as recorded in [ADR-0004](0004-dual-cmake-and-msbuild-support.md).

## Evidence

- Target boundaries: [`CMakeLists.txt`](../../CMakeLists.txt)
- Composition root: [`WindowsAddon.cpp`](../../src/WindowsAddon.cpp)
- Hardware-free CI matrix: [`ci.yml`](../../.github/workflows/ci.yml)
