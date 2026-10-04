#pragma once

#include <QString>
#include <QtGlobal>

// One shot's Decent account upload state, as stored on its shots row
// (migration 42). The serial is the one the shot was uploaded under, so a
// replacement lands on the same machine in the account.
struct DecentUploadState {
    qint64 uploadedAt = 0;       // epoch seconds of the last successful upload; 0 = never
    QString serverShotId;
    QString serial;
    bool replacePending = false;
    int rejectedStatus = 0;      // HTTP status of a permanent rejection; 0 = not rejected
    qint64 rejectedAt = 0;

    bool uploaded() const { return uploadedAt > 0; }
    bool rejected() const { return rejectedStatus > 0; }
};
