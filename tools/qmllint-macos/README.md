# Patched `qmllint` for macOS

A `qmllint` that can analyse **every** file in this tree, including
`qml/components/layout/items/CustomItem.qml`, which the released Qt 6.11.1 `qmllint` cannot.

Without this, macOS builds fall back to `--skip-unlintable` and check 221 of 222 files. With it,
the default build checks 222 of 222 in ~11 s.

## Why it exists

`QQmlJSTypeResolver::merge(QQmlJSRegisterContent, QQmlJSRegisterContent)` recurses into itself
twice — once per `mergeScopes()` call in its return statement — allocating a pool conversion at
every node, so the call tree is `2^depth`. The `a == b` early return is the only bound and does not
fire while scopes keep differing.

Measured on that one 613-line file with the released tool: a **313 GB** physical footprint growing
~30 GB/min, SIGKILLed by the OOM killer after **622.8 s** having emitted 36 of its 122 warnings.
It also drove the whole machine into swap. That is why the fallback is to skip the file rather than
to lint it slowly — a tool that hangs a CI runner for ten minutes is worse than one that checks
221 of 222 and says so.

Fixed upstream by <https://codereview.qt-project.org/c/qt/qtdeclarative/+/755657>.

## What is here, and why it is two pieces

| Path | Size | What it is |
|------|------|-----------|
| `bin/qmllint` | 266 KB | universal (x86_64 + arm64) driver |
| `lib/QtQmlCompiler.framework` | 4.8 MB | universal, **this is where the fix actually lives** |

`QQmlJSTypeResolver` is compiled into `QtQmlCompiler`, not into `qmllint`. Shipping the 266 KB
executable alone would be actively harmful: it would load the *stock* `QtQmlCompiler`, behave
exactly like the released tool, and drive a 313 GB OOM the moment someone turned
`QMLLINT_SKIP_UNLINTABLE` off — while the build config asserted full coverage. If you ever prune
this directory, the two pieces go together or not at all.

`QtQml`, `QtNetwork` and `QtCore` are **not** bundled. They come from the developer's own Qt and
are stock 6.11.1. That mix (patched compiler, stock everything else) is what CMake wires up and
what was verified: 222/222 files linted, 11.0 s, no OOM.

Headers were stripped from the framework; only the binary and `Resources/` are needed at runtime.

## ABI lock — read this before bumping Qt

**These binaries are locked to Qt 6.11.1.** They are the same class of liability as
`android/qt-overrides/`: a Qt bump invalidates them and they must be rebuilt or removed.

CMake asserts the Qt version and falls back to the stock `qmllint` with `--skip-unlintable` and a
loud `WARNING` if it does not match, so a bump degrades coverage rather than breaking the build or
silently loading a mismatched library. That fallback is the safety net, not a licence to leave a
stale binary here.

**Delete this whole directory** (and the `APPLE` branch in `CMakeLists.txt` that stages it) once
the Gerrit change above ships in a Qt release the project targets. It has no reason to outlive the
bug.

## Rebuilding after a Qt bump

Build `qtdeclarative` at the matching tag with the fix applied, then:

```bash
T=tools/qmllint-macos
rm -rf $T/bin $T/lib && mkdir -p $T/bin $T/lib
cp <qtdeclarative-build>/bin/qmllint $T/bin/
cp -R <qtdeclarative-build>/lib/QtQmlCompiler.framework $T/lib/
rm -rf $T/lib/QtQmlCompiler.framework/Versions/A/Headers $T/lib/QtQmlCompiler.framework/Headers
# Pristine rpath: bundle-relative only. CMake adds the machine's Qt lib dir to a STAGED copy,
# so this tracked copy never has to be rewritten and never carries one developer's home path.
install_name_tool -delete_rpath <qtdeclarative-build>/lib $T/bin/qmllint
install_name_tool -delete_rpath <qt>/macos/lib          $T/bin/qmllint
install_name_tool -add_rpath @executable_path/../lib     $T/bin/qmllint
codesign -f -s - $T/bin/qmllint
codesign -f -s - $T/lib/QtQmlCompiler.framework
```

Signatures are ad-hoc. `install_name_tool` invalidates whatever signature a binary had, so the
re-sign is mandatory, not tidiness — an arm64 Mach-O with a broken signature will not execute.

## What this does NOT cover

Linux, Windows and all of CI still run the released `qmllint` with `--skip-unlintable`, so
`CustomItem.qml` is unchecked there. A regression in that one file is caught on a Mac, before it is
pushed, and nowhere else.
