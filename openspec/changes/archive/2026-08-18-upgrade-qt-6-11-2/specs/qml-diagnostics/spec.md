# qml-diagnostics Specification (delta)

Qt 6.11.2's released `qmllint` carries the memoization fix for `QQmlJSRegisterContent::merge()`
(Gerrit 757430, merged onto the `6.11.2` branch 2026-08-11), so the one file no released tool could
finish is now analysable everywhere. The bundled-tool provision and the skip mechanism it was the
alternative to both lose their subject.

## MODIFIED Requirements

### Requirement: The Build Enforces QML Diagnostics
The project SHALL run `qmllint` over every QML file in the module as part of a build target, and
CI SHALL fail when it reports a non-exempt diagnostic.

Coverage SHALL be unconditional: every file in the module, on every platform that runs the gate,
using the `qmllint` that ships with the pinned Qt. No file SHALL be excluded from the run on the
grounds that the tool cannot process it. Should a future toolchain reintroduce such a file, the
correct response is to state the gap and fail — never to exclude the file and report the remainder
as full coverage.

Enforcement goes on with the exemption list sized to the current backlog, so it is real from day
one for everything outside it. This mirrors the `-Wall -Wextra -Werror` contract in
`compiler-diagnostics`, which governs the same question for C++ and deliberately says nothing
about QML.

A developer-only instruction to "run qmllint before pushing" is not enforcement: it is what the
project had when a `ReferenceError` reached a release.

#### Scenario: New QML introduces a non-exempt diagnostic
- **WHEN** a contributor adds QML producing a diagnostic in a category not on the exemption list
- **THEN** the gate fails before the change can merge

#### Scenario: The gate runs without a release tag
- **WHEN** a branch is pushed
- **THEN** the QML diagnostics run, rather than waiting for a tag-triggered release build

#### Scenario: Every file is analysed
- **WHEN** the gate runs on any platform
- **THEN** the file count it reports as analysed equals the number of `.qml` files in
  `qt_add_qml_module`, and no file is reported as skipped

#### Scenario: A run did not finish
- **WHEN** the tool exits non-zero, is killed, or does not reach every file
- **THEN** the run SHALL be reported as failed, and SHALL NOT be used to record or lower any
  recorded count — a file the tool never reached emits no warnings and must never be counted clean

## REMOVED Requirements

### Requirement: A Bundled Tool May Close A Toolchain Gap, But Never Silently

**Reason**: The gap it governed no longer exists. It existed for exactly one file
(`qml/components/layout/items/CustomItem.qml`), which the released `qmllint` could not finish
because `QQmlJSRegisterContent::merge()` was tree-recursive; Qt 6.11.2 memoizes it, and the released
tool now analyses that file along with every other. With the pinned toolchain able to reach full
coverage on its own, a provision permitting a bundled patched tool has no subject — and keeping it
would license shipping a self-built binary for a problem the toolchain has solved.

The requirement's substance is not lost. Its two enduring rules — that coverage gaps are stated
rather than implied, and that an unfinished run must never be read as a clean one — are carried into
the "The Build Enforces QML Diagnostics" requirement above, where they apply unconditionally rather
than only when a bundle is in play.

**Migration**: `tools/qmllint-macos/` is deleted, along with the `QMLLINT_SKIP_UNLINTABLE` CMake
option, the `UNLINTABLE_BY_TOOL_BUG` table, and the `--skip-unlintable` flag in
`scripts/qmllint_report.py`. Contributors who set `QMLLINT_EXECUTABLE` to a hand-built patched
`qmllint` should remove that cache entry and use the `qmllint` beside the pinned Qt. A build
directory configured before this change may still carry a cached `QMLLINT_SKIP_UNLINTABLE`; it is
inert once the option is gone, and can be left or cleared. Should a future Qt reintroduce a file the
released tool cannot analyse, the response is a stated, failing gap — not a reinstated bundle.
