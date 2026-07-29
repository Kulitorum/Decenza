#pragma once

#include <QString>

// Hand a file to the platform's share sheet.
//
// Extracted from BLEManager::shareScaleLog(), which was ~130 lines of Android JNI,
// iOS UIActivityViewController and a desktop fallback wired directly to one
// hard-coded member path. Sharing a file is not a BLE concern, and the moment a
// second caller appeared — the connections page sharing the system log instead of
// the scale log — the alternative was a copy of all three platform branches.
//
// The result is reported through the returned status rather than logged here or
// pushed into a subsystem's log channel, which is what the original did (it emitted
// into the scale log, so a failure to share the scale log was reported into the
// scale log).
namespace FileShare {

struct [[nodiscard]] Result {
    bool ok = false;
    // Ready to show a user. Empty on success where there is nothing to say — the
    // share sheet is its own feedback.
    QString message;
};

// `title` is the chooser title on Android and ignored elsewhere (iOS's activity
// sheet titles itself from the item, desktop has no sheet).
//
// On desktop there is no share sheet, so this succeeds and returns the path in
// `message` for the caller to show — the file IS the deliverable there.
//
// Android's `ok` is not a strong guarantee: a background-activity-launch drop
// (Android 14+, e.g. the app not in foreground when tapped) is not a Java
// exception, so nothing here catches it — that is a platform restriction with
// no callback at all, not a gap in this function. Everything that DOES throw
// (no app can handle ACTION_SEND, URI permission refused, a null chooser) is
// caught and reported through `ok`.
[[nodiscard]] Result shareFile(const QString& filePath, const QString& title);

} // namespace FileShare
