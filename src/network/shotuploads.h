#pragma once

#include "shotuploaddestination.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

class QSqlDatabase;
class SettingsUpload;
class ShotHistoryStorage;

// The one path every shot upload takes, for every destination. It applies the
// shared upload settings (SettingsUpload) once, then queues the shot for each
// active destination, which receives one shot at a time.
//
//  - shotSaved: a finished shot, when automatic upload is on.
//  - an edit (ShotHistoryStorage::shotMetadataUpdated), when automatic update
//    is on: only destinations already holding the shot are updated.
//  - uploadNow: the Upload button, the layout action and MCP.
//  - holdUpdates/expectHeldEdit/releaseUpdates: the review page saves on every
//    field, so while it is open ITS saves are collected and sent once when it
//    closes. Edits from anywhere else still go out at once.
class ShotUploads : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("ShotUploads is created in C++ and reached via MainController")

public:
    ShotUploads(SettingsUpload* settings, ShotHistoryStorage* storage,
                QList<ShotUploadDestination*> destinations, QObject* parent = nullptr);

    // Uploads, or updates, the shot on every active destination now.
    Q_INVOKABLE void uploadNow(qint64 shotId);
    // A shot just finished and saved.
    void shotSaved(qint64 shotId);

    // Names of the destinations switched on and connected.
    QStringList activeDestinations() const;
    // Names of the destinations already holding the shot. Reads only `db`, so it
    // runs on a worker thread.
    QStringList destinationsHolding(QSqlDatabase& db, qint64 shotId) const;
    // Of `holding`, those an edit is sent to now: none while automatic update is off.
    QStringList autoUpdateDestinations(const QStringList& holding) const;

    Q_INVOKABLE void holdUpdates(qint64 shotId);
    // The holding page is about to save the shot; that save waits for release.
    Q_INVOKABLE void expectHeldEdit(qint64 shotId);
    Q_INVOKABLE void releaseUpdates(qint64 shotId);

private:
    using Send = ShotUploadDestination::Send;
    struct Job {
        qint64 shotId;
        Send how;
    };

    void onShotEdited(qint64 shotId, bool success);
    void enqueue(qint64 shotId, Send how);
    void pump(ShotUploadDestination* destination);

    SettingsUpload* m_settings;
    QList<ShotUploadDestination*> m_destinations;
    QHash<ShotUploadDestination*, QList<Job>> m_queues;
    struct Held {
        bool edited = false;   // a page save to send on release
        int pending = 0;       // page saves whose shotMetadataUpdated is still to come
        int uploaded = 0;      // of those, how many an uploadNow already took
    };
    QHash<qint64, Held> m_held;
};
