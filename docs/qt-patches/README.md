# Qt patches kept as source

**Nothing in this directory is built, packaged or shipped.** These are `git format-patch` exports of
Qt changes Decenza once carried as a patched binary, kept so the work is recoverable if a decision
to stop shipping it is reversed. Decenza builds against stock Qt — see the
`Decenza Ships Stock Qt Runtime Binaries` requirement in `openspec/specs/build-config/spec.md`.

A patch here is not an endorsement of applying it. It is a record.

## `qtbase-android-deadlock-protector-no-abort.patch`

**What it fixes.** `QAndroidPlatformOpenGLWindow::eglSurface()` calls `qFatal()` when it cannot
acquire the process-wide `AndroidDeadlockProtector`, killing the app. Losing that race is not a
deadlock — some unrelated subsystem (input method, accessibility, permissions) is simply mid-call
and holding the single global flag. The recoverable path is already written directly below the
`qFatal` and is merely unreachable; the Vulkan backend in the same release takes exactly that path
on the identical condition. The patch's own commit message carries the full reasoning, the field
data and the trade-offs — read it rather than this summary.

- **Upstream bugs**: QTBUG-140490, QTBUG-144207
- **Origin**: qtbase commit `358540b2`, authored 2026-07-27, on top of `bb680db3`
- **Also at**: `github.com/skialpine/qtbase`, branch `a11y/android-talkback-fixes`
- **Touches**: `src/plugins/platforms/android/qandroidplatformopenglwindow.cpp`, +16 −1

**Verified to apply to `v6.11.2`** (`git apply --check`, 2026-08-18). The `qFatal` this replaces is
still present upstream at that tag, at `qandroidplatformopenglwindow.cpp:68`.

**Why it is not shipped.** Decenza shipped it as a patched Android platform plugin in
`android/qt-overrides/` until the Qt 6.11.2 upgrade, which deleted that directory. The other two
patches that directory carried (TalkBack typing echo QTBUG-118858, keyboard-on-accessibility-focus
QTBUG-145786) are upstream in 6.11.2 and are gone for good. This one is not upstream, so dropping it
gives back the crash it prevents — 9 reports in roughly 8 months across a user base in the hundreds,
judged not to justify a permanent fork with an ABI lock and a rebuild obligation on every Qt bump.

**The upstream fix, if it lands, supersedes this.** Gerrit
[735089](https://codereview.qt-project.org/c/qt/qtbase/+/735089) — "Android: drop deadlock protector
from EGL/Vk surface paths" — removes the protector from these paths entirely, which is the better
fix. As of 2026-08-18 it is `NEW` on `dev` with no `Pick-to:` footer. Getting it picked to `6.11`
would land it in 6.11.3 and make this file dead.

**If it has to come back.** Rebuild the Android platform plugin from qtbase at the tag Decenza
builds against:

```
git checkout v<qt-version> && git am <this-patch>
```

then restore the `android-release.yml` override step *together with* the `BUILT_AGAINST_QT`
version-lock guard that fails the build on a Qt/artifact mismatch — the guard is what keeps a stale
plugin from producing an APK that dies at startup on every device, and it must not be reintroduced
without it. The spec requires the change that does this to state the observed crash rate it is
responding to.
