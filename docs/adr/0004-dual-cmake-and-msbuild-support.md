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
trees, package caches, and compiler output (`.pdb`, `.exp`, `.res`) are never
versioned. Two prebuilt import libraries the native build links against,
`hidapi.lib` and `BSECLibrary64.lib`, are the deliberate exception: they are
tracked so a fresh clone builds in Visual Studio without a vcpkg step.

Third-party dependencies come from vcpkg or an explicit external path. In
particular, BSEC is opt-in and its proprietary binary is supplied using
`WINDOWSADDON_BSEC_LIBRARY` in CMake or `BSEC_LIBRARY_DIR` in MSBuild.

## Consequences

- Contributors can use either the native solution or the portable CMake flow.
- Project-file drift is a maintenance risk. CI validates CMake; Windows changes
  should also receive a native solution build before release.
- Local package caches, the abandoned `UnitTests/` MSBuild project and compiler
  output were untracked on 2026-09-05; `.gitignore` keeps them out.

## Evidence

- Portable build: [`CMakeLists.txt`](../../CMakeLists.txt)
- Native build: [`WindowsAddon.vcxproj`](../../WindowsAddon.vcxproj)
- Ignored generated files: [`.gitignore`](../../.gitignore)
