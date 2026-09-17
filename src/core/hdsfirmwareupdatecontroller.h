#pragma once

#include "hdsfirmwarecatalog.h"

#include <QObject>
#include <QPointer>
#include <QtQml/qqmlregistration.h>

class QNetworkAccessManager;
class QNetworkReply;
class ScaleDevice;

// Once a start request is dispatched, the scale may refuse it two different
// ways: synchronously and in-band (only WiFi has this reply channel — see
// ScaleDevice::firmwareUpdateRejected / updateError below), or asynchronously
// after already accepting the request, once it has checked its own signed
// catalog and closed its transport clients (openscale's pullOtaFail — reaches
// only the scale's own display, never any transport this controller reads).
// The second kind is a silent no-op here by construction, not an oversight;
// see HdsFirmwareCatalog::isPreviewOrRcVersion's comment for the concrete
// case that puts a user in front of it.
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
    Q_PROPERTY(QString updateError READ updateError NOTIFY updateErrorChanged)

public:
    explicit HdsFirmwareUpdateController(QNetworkAccessManager* networkManager, QObject* parent = nullptr);

    bool checking() const { return m_checking; }
    bool updateAvailable() const { return m_updateAvailable; }
    QString installedVersion() const;
    QString availableVersion() const;
    QString releaseNotes() const { return m_releaseNotes; }
    bool releaseNotesLoading() const { return m_releaseNotesLoading; }
    // True once a start request has been DISPATCHED — not once the scale has
    // accepted it; only a synchronous refusal (see updateError below) is ever
    // knowable, so this flips back to false on that and stays true otherwise.
    // The scale reports genuine acceptance as QUEUED, never as installed, and
    // no transport carries a progress stream, so even while true this must
    // never be read as a completed, or even a confirmed-accepted, update.
    bool updateStarted() const { return m_updateStarted; }
    // Non-empty when the scale explicitly, synchronously refused the request
    // (see ScaleDevice::firmwareUpdateRejected) — currently only reachable
    // over WiFi. Empty otherwise, including while a request is genuinely in
    // flight or accepted: silence is not evidence of success on any
    // transport, only an explicit refusal is evidence of failure.
    QString updateError() const { return m_updateError; }

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
    void updateErrorChanged();
    void activeScaleChanged();

private slots:
    void onFirmwareUpdateRejected(const QString& reason);

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
    QString m_updateError;
};
