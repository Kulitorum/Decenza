#include "hdsfirmwareupdatecontroller.h"

#include "githubreleaseclient.h"
#include "ble/scaledevice.h"
#include "ble/scales/scalelogging.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>

namespace {

// Aliases, not copies — the shared helpers already carry the registered marker
// and the tier rules (see scalelogging.h). These four sites used to hand-type
// "[Scale][HDS Update]" onto a bare qWarning, which is the drift the marker gate
// exists to catch; it only passed because this file is in neither glob set.
// The _STDERR variant because this controller has no logMessage signal — the
// emitting form is for the scale drivers, which do.
#define HDS_UPDATE_WARN(msg) SCALE_WARN_STDERR_TAGGED("HDS Update", msg)
// Same reasoning, INFO tier: a check's OUTCOME is the user's story (did we
// find an update, and to what) the same way "connected"/"dropped" is for a
// transport — see scalelogging.h's audience rule. Without this, a check that
// succeeds leaves no trace at all: only failure had a line.
#define HDS_UPDATE_INFO(msg) SCALE_INFO_STDERR_TAGGED("HDS Update", msg)

constexpr auto kManifestUrl = "https://github.com/decentespresso/openscale/releases/latest/download/manifest.json";
constexpr auto kOpenScaleRepository = "decentespresso/openscale";

QString releaseTag(const QString& version)
{
    return version.startsWith(QLatin1Char('v')) ? version : QStringLiteral("v") + version;
}

} // namespace

HdsFirmwareUpdateController::HdsFirmwareUpdateController(QNetworkAccessManager* networkManager, QObject* parent)
    : QObject(parent)
    , m_network(networkManager)
{
    Q_ASSERT(m_network);
    checkForUpdates();
}

QString HdsFirmwareUpdateController::installedVersion() const
{
    return m_scale ? m_scale->firmwareVersion() : QString();
}

QString HdsFirmwareUpdateController::availableVersion() const
{
    return m_availableRelease ? m_availableRelease->version : QString();
}

void HdsFirmwareUpdateController::setScaleDevice(ScaleDevice* scale)
{
    if (m_scale == scale)
        return;
    if (m_scale)
        disconnect(m_scale, nullptr, this, nullptr);
    cancelReleaseNotesRequest();
    // Unconditional, not left to reevaluateAvailability()'s own reset: that one
    // only fires when availability or its version actually differs, so two
    // scales that happen to report the same eligibility (e.g. both on the same
    // stale firmware) would otherwise carry updateStarted/updateError from
    // the OLD scale onto a new one that was never asked anything.
    if (m_updateStarted) {
        m_updateStarted = false;
        emit updateStartedChanged();
    }
    if (!m_updateError.isEmpty()) {
        m_updateError.clear();
        emit updateErrorChanged();
    }
    m_scale = scale;
    if (m_scale) {
        connect(m_scale, &ScaleDevice::connectedChanged, this, &HdsFirmwareUpdateController::reevaluateAvailability);
        connect(m_scale, &ScaleDevice::firmwareVersionChanged, this, &HdsFirmwareUpdateController::reevaluateAvailability);
        connect(m_scale, &ScaleDevice::firmwareUpdateRejected, this, &HdsFirmwareUpdateController::onFirmwareUpdateRejected);
    }
    reevaluateAvailability();
    emit activeScaleChanged();
}

void HdsFirmwareUpdateController::checkForUpdates()
{
    if (m_manifestReply)
        m_manifestReply->abort();
    m_checking = true;
    emit checkingChanged();

    QNetworkReply* reply = m_network->get(GitHubReleaseClient::requestForUrl(
        QUrl(QString::fromLatin1(kManifestUrl))));
    m_manifestReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply != m_manifestReply) {
            reply->deleteLater();
            return;
        }
        m_manifestReply = nullptr;
        m_checking = false;
        emit checkingChanged();
        if (!reply)
            return;
        if (reply->error() != QNetworkReply::NoError) {
            HDS_UPDATE_WARN(QStringLiteral("Manifest check failed: %1").arg(reply->errorString()));
            reply->deleteLater();
            return;
        }
        QString error;
        const auto catalog = HdsFirmwareCatalog::fromJson(reply->readAll(), &error);
        reply->deleteLater();
        if (!catalog) {
            HDS_UPDATE_WARN(QStringLiteral("Manifest ignored: %1").arg(error));
            return;
        }
        m_catalog = catalog;
        reevaluateAvailability();
        if (!m_scale || !m_scale->isConnected()) {
            HDS_UPDATE_INFO(QStringLiteral("Checked manifest: no scale connected to evaluate against"));
        } else if (m_updateAvailable) {
            HDS_UPDATE_INFO(QStringLiteral("Checked manifest: update available, %1 -> %2")
                             .arg(installedVersion(), availableVersion()));
        } else {
            HDS_UPDATE_INFO(QStringLiteral("Checked manifest: up to date at %1").arg(installedVersion()));
        }
    });
}

void HdsFirmwareUpdateController::loadReleaseNotes()
{
    if (!m_availableRelease || m_releaseNotesLoading || !m_releaseNotes.isEmpty())
        return;
    m_releaseNotesLoading = true;
    emit releaseNotesLoadingChanged();
    QNetworkReply* reply = m_network->get(GitHubReleaseClient::releaseRequest(
        QString::fromLatin1(kOpenScaleRepository), releaseTag(m_availableRelease->version)));
    m_releaseNotesReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply != m_releaseNotesReply) {
            reply->deleteLater();
            return;
        }
        m_releaseNotesReply = nullptr;
        m_releaseNotesLoading = false;
        emit releaseNotesLoadingChanged();
        if (!reply)
            return;
        if (reply->error() != QNetworkReply::NoError) {
            HDS_UPDATE_WARN(QStringLiteral("Release notes check failed: %1").arg(reply->errorString()));
            reply->deleteLater();
            return;
        }
        QString error;
        const auto release = GitHubReleaseClient::parseRelease(reply->readAll(), &error);
        reply->deleteLater();
        if (!release) {
            HDS_UPDATE_WARN(QStringLiteral("Release notes ignored: %1").arg(error));
            return;
        }
        m_releaseNotes = release->body;
        emit releaseNotesChanged();
    });
}

void HdsFirmwareUpdateController::startUpdate()
{
    reevaluateAvailability();
    if (!m_updateAvailable || !m_scale || m_updateStarted)
        return;
    if (!m_updateError.isEmpty()) {
        m_updateError.clear();
        emit updateErrorChanged();
    }
    // Set BEFORE dispatching, not after: DecentScaleWifi can emit
    // firmwareUpdateRejected() synchronously, from inside startFirmwareUpdate()
    // itself, when the send fails outright (socket not connected). If
    // m_updateStarted weren't already true by then, onFirmwareUpdateRejected()'s
    // own "nothing in flight" guard would silently drop exactly the failure
    // this signal exists to report.
    m_updateStarted = true;
    emit updateStartedChanged();
    // Name the release. The scale then installs it without showing its own
    // picker or asking for a hold-to-confirm, and re-resolves the version
    // against its own signed catalog before writing anything.
    m_scale->startFirmwareUpdate(m_availableRelease->version);
}

void HdsFirmwareUpdateController::onFirmwareUpdateRejected(const QString& reason)
{
    // Only meaningful while we're actually waiting on a request we made —
    // ignore a rejection signal reaching us outside that window (e.g. a
    // stale/duplicate emission) rather than overwriting a since-cleared state.
    //
    // Accepted gap: reevaluateAvailability() can also reset m_updateStarted to
    // false on its own (a version or availability change — e.g. the scale
    // reconnecting) without ever setting m_updateError, so a genuine rejection
    // whose delivery races that reset is dropped silently rather than shown.
    // Narrow — the WiFi reply this guards is near-immediate — and no worse
    // than the pre-existing "silence is not success either" contract the rest
    // of this class already accepts; not worth a lock for.
    if (!m_updateStarted)
        return;
    m_updateStarted = false;
    m_updateError = reason;
    emit updateStartedChanged();
    emit updateErrorChanged();
}

void HdsFirmwareUpdateController::cancelReleaseNotesRequest()
{
    if (m_releaseNotesReply) {
        QNetworkReply* reply = m_releaseNotesReply;
        m_releaseNotesReply = nullptr;
        reply->abort();
    }
    if (!m_releaseNotesLoading)
        return;
    m_releaseNotesLoading = false;
    emit releaseNotesLoadingChanged();
}

void HdsFirmwareUpdateController::reevaluateAvailability()
{
    const auto oldVersion = availableVersion();
    const bool wasAvailable = m_updateAvailable;
    m_availableRelease.reset();
    if (m_catalog && m_scale && m_scale->isConnected() && m_scale->supportsFirmwareUpdate())
        m_availableRelease = m_catalog->newestEligibleRelease(m_scale->firmwareVersion());
    setUpdateAvailable(m_availableRelease.has_value());
    if (wasAvailable != m_updateAvailable || oldVersion != availableVersion()) {
        cancelReleaseNotesRequest();
        m_releaseNotes.clear();
        m_updateStarted = false;
        m_updateError.clear();
        emit releaseNotesChanged();
        emit updateStartedChanged();
        emit updateErrorChanged();
        emit availabilityChanged();
    }
}

void HdsFirmwareUpdateController::setUpdateAvailable(bool available)
{
    if (m_updateAvailable == available)
        return;
    m_updateAvailable = available;
    emit updateAvailableChanged();
}
