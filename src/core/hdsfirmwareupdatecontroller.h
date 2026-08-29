#pragma once

#include "hdsfirmwarecatalog.h"

#include <QObject>
#include <QPointer>
#include <QtQml/qqmlregistration.h>

class QNetworkAccessManager;
class QNetworkReply;
class ScaleDevice;

class HdsFirmwareUpdateController : public QObject {
    Q_OBJECT

    QML_ELEMENT
    QML_UNCREATABLE("HdsFirmwareUpdateController is created in C++ and reached via MainController")

    Q_PROPERTY(bool checking READ checking NOTIFY checkingChanged)
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY updateAvailableChanged)
    Q_PROPERTY(QString installedVersion READ installedVersion NOTIFY availabilityChanged)
    Q_PROPERTY(QString availableVersion READ availableVersion NOTIFY availabilityChanged)
    Q_PROPERTY(QString releaseNotes READ releaseNotes NOTIFY releaseNotesChanged)
    Q_PROPERTY(bool releaseNotesLoading READ releaseNotesLoading NOTIFY releaseNotesLoadingChanged)
    Q_PROPERTY(bool updateStarted READ updateStarted NOTIFY updateStartedChanged)

public:
    explicit HdsFirmwareUpdateController(QNetworkAccessManager* networkManager, QObject* parent = nullptr);

    bool checking() const { return m_checking; }
    bool updateAvailable() const { return m_updateAvailable; }
    QString installedVersion() const;
    QString availableVersion() const;
    QString releaseNotes() const { return m_releaseNotes; }
    bool releaseNotesLoading() const { return m_releaseNotesLoading; }
    // True once the scale has accepted a start request. The scale reports a
    // request as QUEUED, never as installed, and no transport carries a
    // progress stream, so this must never be read as a completed update.
    bool updateStarted() const { return m_updateStarted; }

    void setScaleDevice(ScaleDevice* scale);

public slots:
    void checkForUpdates();
    void loadReleaseNotes();
    void startUpdate();

signals:
    void checkingChanged();
    void updateAvailableChanged();
    void availabilityChanged();
    void releaseNotesChanged();
    void releaseNotesLoadingChanged();
    void updateStartedChanged();
    void activeScaleChanged();

private:
    void cancelReleaseNotesRequest();
    void reevaluateAvailability();
    void setUpdateAvailable(bool available);

    QNetworkAccessManager* m_network = nullptr;
    QPointer<ScaleDevice> m_scale;
    QPointer<QNetworkReply> m_manifestReply;
    QPointer<QNetworkReply> m_releaseNotesReply;
    std::optional<HdsFirmwareCatalog> m_catalog;
    std::optional<HdsFirmwareRelease> m_availableRelease;
    bool m_checking = false;
    bool m_updateAvailable = false;
    bool m_releaseNotesLoading = false;
    bool m_updateStarted = false;
    QString m_releaseNotes;
};
