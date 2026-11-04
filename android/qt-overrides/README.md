# Android QtQml override

The release workflow substitutes `libQt6Qml_arm64-v8a.so` for the stock Qt 6.12.0
Android arm64 library. It is an optimized library with GC diagnostics, not a
Debug ABI build. Other Qt libraries remain stock.

## Pending replacement

The currently checked-in binary has build ID
`7d891152fd8098e6aa05387fe647fee609e0f866`. It contains the original diagnostic
patch, **not yet the allocator fix or weak-scan history below**. Replacement is
pending an Android build; Qt Creator currently rejects its automatically added
APK packaging step before starting compilation. Do not describe this binary as
containing the new fix until it has been rebuilt and verified.

## Source and build

Start from the Qt 6.12.0 SDK's `Src/qtdeclarative` in an isolated copy. Apply these
patches from `tools/qt-gc-diagnostics/`, in order, using `patch -p1`:

1. `qt-6.12.0-gc-diagnostics.patch`
2. `qt-6.12.0-gc-weak-history.patch`
3. `qt-6.12.0-sweep-allocation-fix.patch`

The third patch backports [QTBUG-149512](https://github.com/qt/qtdeclarative/commit/059ee9db91507b8b86cdf4bf033985c9ea41e0d4):
allocations made by destructors during sweep retain their allocator bitmap entries.
Use the stock Android arm64 Qt 6.12.0 kit, NDK 27.2.12479018, Android API 28,
shared libc++, and Release optimization (`-Oz -DNDEBUG`). Build the `Qml` target
without installing into or modifying the SDK. Disable the IDE's APK build step
for this library-only project. Qt Quick is unnecessary: disabling discovery of
`Qt6ShaderTools` and `Qt6ShaderToolsTools` excludes it without changing Qml features.
Never define `DECENZA_QML_GC_TEST_HOOKS` in the Android build.

Before replacing the artifact, compare exported symbols, QtQml feature headers,
SONAME, dependencies and ELF architecture with the stock SDK. Record the artifact
and patch SHA-256 hashes, build ID, toolchain and diagnostic revision in
`manifest.json`; `tools/qt-gc-diagnostics/verify_artifact.py` validates that record.
Keep the unstripped build output for subsequent native stack symbolication.

## Additional evidence if the crash persists

The new startup marker is `gcdiag-v2 sweep-fix=QTBUG-149512 weak-history=1`.
For each missed QObject wrapper, `WEAK_HISTORY` compares the original incremental
weak scan with the later full mark: object/top-parent addresses, ownership flags,
keep-alive decision and mark bits. A compact summary is also included before the
retaining path in the deferred-abort message, within the crash report's prefix.
This distinguishes an ownership change after scanning from a wrapper that was
already retained and marked at its last recorded scan.

History is limited to 200,000 wrappers per engine per collection. `incomplete=1`
and possible address reuse limit conclusions; `seen=0` alone is not proof that
Qt skipped an object. Existing full-mark preservation and abort-after-fresh-idle
behavior remain in place. Diagnostic logging occurs on mismatches; collecting
history adds bounded native allocations and lookup overhead during normal GC.

## Validation and removal

Patched macOS QtQml passes the allocator regression and diagnostic controls
(Qt Creator runs `1793559335430` and `1793559335431`). Lost extent slots and free
runs inside live objects fall from 8/8 on stock Qt to 0/0 with the fix. These
results do not prove the cause of #2179. Android runtime and crash-report checks
remain required, including confirmation of the new startup marker.

Retire the allocator backport when the selected stock Qt includes the upstream
fix and passes the regression. Retire the diagnostic override separately when
field evidence resolves the missed-mark reports; upgrading Qt alone does not
establish that the ownership-change gap has been fixed.
