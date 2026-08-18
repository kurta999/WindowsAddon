# Architecture decision records

Architecture decision records (ADRs) capture choices that affect dependency
direction, runtime safety, or contributor workflow. They describe why a choice
was made and its costs; implementation detail remains in code and tests.

| ADR | Status | Decision |
| --- | --- | --- |
| [0001](0001-headless-libraries-and-composition-root.md) | Accepted | Split testable headless libraries from the wxWidgets composition root |
| [0002](0002-typed-worker-to-gui-publication.md) | Accepted | Publish typed owning values from workers and marshal GUI updates |
| [0003](0003-length-bounded-byte-oriented-parsing.md) | Accepted | Parse wire data exclusively through explicit lengths |
| [0004](0004-dual-cmake-and-msbuild-support.md) | Accepted | Keep CMake and the native Visual Studio project supported together |

Use the next zero-padded number for a new decision. Superseded records remain
in this directory and link to their replacement.
