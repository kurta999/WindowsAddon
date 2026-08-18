# ADR-0004: Maintain CMake and native Visual Studio builds

- Status: Accepted
- Date: 2026-08-12

## Context

CMake drives portable builds, dependency-free tests, instrumentation, fuzzing,
and CI. The native Visual Studio solution remains useful for the established
Windows development workflow and its named configurations.

## Decision

Keep `WindowsAddon.sln`, `WindowsAddon.vcxproj`, and its filters as supported
build metadata alongside CMake. Production source additions and removals must
be reflected in both systems. Per-user `.vcxproj.user` files, generated build
trees, package caches, and compiled libraries are never versioned.

Third-party dependencies come from vcpkg or an explicit external path. In
particular, BSEC is opt-in and its proprietary binary is supplied using
`WINDOWSADDON_BSEC_LIBRARY` in CMake or `BSEC_LIBRARY_DIR` in MSBuild.

## Consequences

- Contributors can use either the native solution or the portable CMake flow.
- Project-file drift is a maintenance risk. CI validates CMake; Windows changes
  should also receive a native solution build before release.
- Repository history no longer carries local package caches or compiler output.

## Evidence

- Portable build: [`CMakeLists.txt`](../../CMakeLists.txt)
- Native build: [`WindowsAddon.vcxproj`](../../WindowsAddon.vcxproj)
- Ignored generated files: [`.gitignore`](../../.gitignore)
