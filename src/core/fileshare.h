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

struct Result {
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
Result shareFile(const QString& filePath, const QString& title);

} // namespace FileShare
