# Qt patches kept as source

**Nothing here is built, packaged or shipped.** This records Qt changes Decenza once carried as a
patched binary: where each one went, and where to recover it. Decenza builds against stock Qt — see
the `Decenza Ships Stock Qt Runtime Binaries` requirement in `openspec/specs/build-config/spec.md`.
No patch is kept in this directory today.

## Android deadlock-protector abort (fixed upstream in Qt 6.12.1)

`QAndroidPlatformOpenGLWindow::eglSurface()` called `qFatal()` when it could not acquire the
process-wide `AndroidDeadlockProtector` (QTBUG-140490, QTBUG-144207; field reports on
[#1663](https://github.com/Kulitorum/Decenza/issues/1663)). Decenza shipped a patch for it until
Qt 6.11.2, then kept it here as source only.

Qt 6.12.1 removes the cause. Gerrit 775580 (`dev`) rewrote the Android surface lifecycle for
single-thread ownership, and its cherry-pick 782087 merged on `6.12`. In the 6.12.1 source,
`qtbase/src/plugins/platforms/android/qandroidplatformopenglwindow.cpp` no longer acquires the
protector, and "Failed to acquire deadlock protector" appears nowhere in the Android platform
plugin. The patch was deleted with the 6.12.1 upgrade; it is in git history before that change.

## QtQml garbage-collection diagnostics

The instrumented Android `libQt6Qml` that shipped for #2179 was retired with the Qt 6.12.1
upgrade. Its patches, rebuild recipe and recovery commits (its tests are at `62fef1177`) are kept beside the probes that
exercise them, in [`tools/qt-gc-diagnostics/`](../../tools/qt-gc-diagnostics/README.md).
