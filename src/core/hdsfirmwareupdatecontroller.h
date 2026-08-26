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
    Q_PROPERTY(bool handoffStarted READ handoffStarted NOTIFY handoffStartedChanged)

public:
    explicit HdsFirmwareUpdateController(QNetworkAccessManager* networkManager, QObject* parent = nullptr);

    bool checking() const { return m_checking; }
    bool updateAvailable() const { return m_updateAvailable; }
    QString installedVersion() const;
    QString availableVersion() const;
    QString releaseNotes() const { return m_releaseNotes; }
    bool releaseNotesLoading() const { return m_releaseNotesLoading; }
    bool handoffStarted() const { return m_handoffStarted; }

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
    void handoffStartedChanged();
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
    bool m_handoffStarted = false;
    QString m_releaseNotes;
};
