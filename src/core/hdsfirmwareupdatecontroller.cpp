#include "hdsfirmwareupdatecontroller.h"

#include "githubreleaseclient.h"
#include "ble/scaledevice.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QDebug>

namespace {

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
    m_scale = scale;
    if (m_scale) {
        connect(m_scale, &ScaleDevice::connectedChanged, this, &HdsFirmwareUpdateController::reevaluateAvailability);
        connect(m_scale, &ScaleDevice::firmwareVersionChanged, this, &HdsFirmwareUpdateController::reevaluateAvailability);
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
            qWarning().noquote() << "[Scale][HDS Update] Manifest check failed:" << reply->errorString();
            reply->deleteLater();
            return;
        }
        QString error;
        const auto catalog = HdsFirmwareCatalog::fromJson(reply->readAll(), &error);
        reply->deleteLater();
        if (!catalog) {
            qWarning().noquote() << "[Scale][HDS Update] Manifest ignored:" << error;
            return;
        }
        m_catalog = catalog;
        reevaluateAvailability();
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
            qWarning().noquote() << "[Scale][HDS Update] Release notes check failed:" << reply->errorString();
            reply->deleteLater();
            return;
        }
        QString error;
        const auto release = GitHubReleaseClient::parseRelease(reply->readAll(), &error);
        reply->deleteLater();
        if (!release) {
            qWarning().noquote() << "[Scale][HDS Update] Release notes ignored:" << error;
            return;
        }
        m_releaseNotes = release->body;
        emit releaseNotesChanged();
    });
}

void HdsFirmwareUpdateController::startUpdate()
{
    reevaluateAvailability();
    if (!m_updateAvailable || !m_scale || m_handoffStarted)
        return;
    m_scale->startFirmwareUpdate();
    m_handoffStarted = true;
    emit handoffStartedChanged();
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
        m_handoffStarted = false;
        emit releaseNotesChanged();
        emit handoffStartedChanged();
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
