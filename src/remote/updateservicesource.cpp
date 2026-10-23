#include "updateservicesource.h"
#include "updateoperationpolicy.h"

#include "ble/de1device.h"
#include "controllers/firmwareupdater.h"
#include "core/settings_app.h"
#include "core/updatechecker.h"

#include <QMetaType>

#include <utility>

namespace {
QString startReason(ServiceRequestIds::Start start)
{
    switch (start) {
    case ServiceRequestIds::Start::InvalidId: return QStringLiteral("invalidRequest");
    case ServiceRequestIds::Start::DuplicateId: return QStringLiteral("duplicateRequest");
    case ServiceRequestIds::Start::LimitReached: return QStringLiteral("tooManyRequests");
    case ServiceRequestIds::Start::Accepted: break;
    }
    return {};
}
}

NativeUpdateServiceSource::NativeUpdateServiceSource(
    UpdateChecker* appUpdater, FirmwareUpdater* firmwareUpdater,
    SettingsApp* settingsApp, DE1Device* device,
    std::function<void()> checkAllUpdates, QObject* parent)
    : UpdateStateSimpleSource(parent), m_appUpdater(appUpdater),
      m_firmwareUpdater(firmwareUpdater), m_settingsApp(settingsApp), m_device(device),
      m_checkAllUpdates(std::move(checkAllUpdates))
{
    if (m_appUpdater) {
        connect(m_appUpdater, &UpdateChecker::checkingChanged, this, &NativeUpdateServiceSource::publish);
        connect(m_appUpdater, &UpdateChecker::downloadingChanged, this, &NativeUpdateServiceSource::publish);
        connect(m_appUpdater, &UpdateChecker::downloadProgressChanged, this, &NativeUpdateServiceSource::publish);
        connect(m_appUpdater, &UpdateChecker::updateAvailableChanged, this, &NativeUpdateServiceSource::publish);
        connect(m_appUpdater, &UpdateChecker::latestVersionChanged, this, &NativeUpdateServiceSource::publish);
        connect(m_appUpdater, &UpdateChecker::latestVersionCodeChanged, this, &NativeUpdateServiceSource::publish);
        connect(m_appUpdater, &UpdateChecker::releaseNotesChanged, this, &NativeUpdateServiceSource::publish);
        connect(m_appUpdater, &UpdateChecker::errorMessageChanged, this, &NativeUpdateServiceSource::publish);
        connect(m_appUpdater, &UpdateChecker::installingChanged, this, &NativeUpdateServiceSource::publish);
        connect(m_appUpdater, &UpdateChecker::latestIsBetaChanged, this, &NativeUpdateServiceSource::publish);
        connect(m_appUpdater, &UpdateChecker::downloadReadyChanged, this, &NativeUpdateServiceSource::publish);
        connect(m_appUpdater, &UpdateChecker::canDownloadUpdateChanged, this, &NativeUpdateServiceSource::publish);
        connect(m_appUpdater, &QObject::destroyed, this, [this] { m_appUpdater = nullptr; publish(); });
    }
    if (m_firmwareUpdater) {
        m_installedVersion = m_firmwareUpdater->installedVersion();
        connect(m_firmwareUpdater, &FirmwareUpdater::stateChanged, this, &NativeUpdateServiceSource::publish);
        connect(m_firmwareUpdater, &FirmwareUpdater::availabilityChanged, this, &NativeUpdateServiceSource::publish);
        connect(m_firmwareUpdater, &FirmwareUpdater::progressChanged, this, &NativeUpdateServiceSource::publish);
        connect(m_firmwareUpdater, &FirmwareUpdater::installedVersionChanged, this, [this] {
            if (m_firmwareUpdater) m_installedVersion = m_firmwareUpdater->installedVersion();
            publish();
        });
        connect(m_firmwareUpdater, &FirmwareUpdater::isSimulatedChanged, this, &NativeUpdateServiceSource::publish);
        connect(m_firmwareUpdater, &FirmwareUpdater::needsManualRebootChanged, this, &NativeUpdateServiceSource::publish);
        connect(m_firmwareUpdater, &QObject::destroyed, this, [this] { m_firmwareUpdater = nullptr; publish(); });
    }
    if (m_device)
        connect(m_device, &DE1Device::serialNumberChanged, this, &NativeUpdateServiceSource::publish);
    if (m_settingsApp) {
        connect(m_settingsApp, &SettingsApp::developerTranslationUploadChanged,
                this, &NativeUpdateServiceSource::publish);
        connect(m_settingsApp, &QObject::destroyed, this, [this] { m_settingsApp = nullptr; publish(); });
    }
    publish();
}

QVariantMap NativeUpdateServiceSource::capture() const
{
    QVariantMap app;
    if (m_appUpdater) {
        app = {
            {QStringLiteral("checking"), m_appUpdater->isChecking()},
            {QStringLiteral("downloading"), m_appUpdater->isDownloading()},
            {QStringLiteral("downloadProgress"), m_appUpdater->downloadProgress()},
            {QStringLiteral("updateAvailable"), m_appUpdater->isUpdateAvailable()},
            {QStringLiteral("latestVersion"), m_appUpdater->latestVersion()},
            {QStringLiteral("currentVersion"), m_appUpdater->currentVersion()},
            {QStringLiteral("currentVersionCode"), m_appUpdater->currentVersionCode()},
            {QStringLiteral("latestVersionCode"), m_appUpdater->latestVersionCode()},
            {QStringLiteral("releaseNotes"), m_appUpdater->releaseNotes()},
            {QStringLiteral("errorMessage"), m_appUpdater->errorMessage()},
            {QStringLiteral("canDownloadUpdate"), m_appUpdater->canDownloadUpdate()},
            {QStringLiteral("canCheckForUpdates"), m_appUpdater->canCheckForUpdates()},
            {QStringLiteral("downloadReady"), m_appUpdater->isDownloadReady()},
            {QStringLiteral("platformName"), m_appUpdater->platformName()},
            {QStringLiteral("releasePageUrl"), m_appUpdater->releasePageUrl()},
            {QStringLiteral("latestIsBeta"), m_appUpdater->latestIsBeta()},
            {QStringLiteral("installing"), m_appUpdater->isInstalling()}
        };
    }
    QVariantMap firmware;
    if (m_firmwareUpdater) {
        firmware = {
            {QStringLiteral("state"), static_cast<int>(m_firmwareUpdater->state())},
            {QStringLiteral("stateText"), m_firmwareUpdater->stateText()},
            {QStringLiteral("updateAvailable"), m_firmwareUpdater->updateAvailable()},
            {QStringLiteral("isDowngrade"), m_firmwareUpdater->isDowngrade()},
            {QStringLiteral("isReflash"), m_firmwareUpdater->isReflash()},
            {QStringLiteral("isSimulated"), m_firmwareUpdater->isSimulated()},
            {QStringLiteral("isFlashing"), m_firmwareUpdater->isFlashing()},
            {QStringLiteral("availableVersion"), m_firmwareUpdater->availableVersion()},
            {QStringLiteral("availableVersionLabel"), m_firmwareUpdater->availableVersionLabel()},
            {QStringLiteral("availableChannelLabel"), m_firmwareUpdater->availableChannelLabel()},
            {QStringLiteral("availableReleaseNotes"), m_firmwareUpdater->availableReleaseNotes()},
            {QStringLiteral("installedVersion"), m_installedVersion},
            {QStringLiteral("progress"), m_firmwareUpdater->progress()},
            {QStringLiteral("errorMessage"), m_firmwareUpdater->errorMessage()},
            {QStringLiteral("retryAvailable"), m_firmwareUpdater->retryAvailable()},
            {QStringLiteral("needsManualReboot"), m_firmwareUpdater->needsManualReboot()}
        };
    }
    return {
        {QStringLiteral("app"), app},
        {QStringLiteral("firmware"), firmware},
        {QStringLiteral("deviceSerialNumber"), m_device ? m_device->serialNumber().left(64) : QString()},
        {QStringLiteral("developerTranslationUpload"), m_settingsApp && m_settingsApp->developerTranslationUpload()}
    };
}

void NativeUpdateServiceSource::publish()
{
    const bool available = m_appUpdater && m_firmwareUpdater && m_settingsApp;
    setSnapshot(capture());
    setReady(available);
}

void NativeUpdateServiceSource::refresh() { publish(); }

void NativeUpdateServiceSource::finish(const QString& id, const QString& operation,
                                       bool success, const QVariantMap& result,
                                       const QString& reason)
{
    if (!m_requests.finish(id)) return;
    publish();
    emit operationCompleted(id, operation, success, result, reason);
}

void NativeUpdateServiceSource::execute(QString id, QString operation, QVariantMap args)
{
    const auto start = m_requests.begin(id);
    if (start == ServiceRequestIds::Start::DuplicateId) return;
    if (start != ServiceRequestIds::Start::Accepted) {
        emit operationCompleted(id, operation, false, {}, startReason(start));
        return;
    }
    if (!UpdateOperationPolicy::supported(operation)) {
        finish(id, operation, false, {}, QStringLiteral("unsupportedOperation")); return;
    }
    if (!UpdateOperationPolicy::validArguments(operation, args)) {
        finish(id, operation, false, {}, QStringLiteral("invalidArguments")); return;
    }
    if (operation == QLatin1String("checkAll")) {
        if (!m_checkAllUpdates) { finish(id, operation, false, {}, QStringLiteral("unavailable")); return; }
        m_checkAllUpdates();
    } else if (operation == QLatin1String("checkApp")) {
        if (!m_appUpdater) { finish(id, operation, false, {}, QStringLiteral("unavailable")); return; }
        if (!m_appUpdater->canCheckForUpdates()) { finish(id, operation, false, {}, QStringLiteral("unsupportedPlatform")); return; }
        if (m_appUpdater->isChecking() || m_appUpdater->isDownloading()) { finish(id, operation, false, {}, QStringLiteral("busy")); return; }
        m_appUpdater->checkForUpdates();
    } else if (operation == QLatin1String("downloadAndInstall")) {
        if (!m_appUpdater) { finish(id, operation, false, {}, QStringLiteral("unavailable")); return; }
        if (!m_appUpdater->canDownloadUpdate() || !m_appUpdater->isUpdateAvailable()
            || m_appUpdater->isChecking() || m_appUpdater->isDownloading() || m_appUpdater->isInstalling()) {
            finish(id, operation, false, {}, QStringLiteral("unavailable")); return;
        }
        m_appUpdater->downloadAndInstall();
    } else if (operation == QLatin1String("dismissAppUpdate")) {
        if (!m_appUpdater) { finish(id, operation, false, {}, QStringLiteral("unavailable")); return; }
        m_appUpdater->dismissUpdate();
    } else if (operation == QLatin1String("checkFirmware")) {
        if (!m_firmwareUpdater) { finish(id, operation, false, {}, QStringLiteral("unavailable")); return; }
        const auto state = m_firmwareUpdater->state();
        if (state == FirmwareUpdater::State::Checking || state == FirmwareUpdater::State::Downloading
            || m_firmwareUpdater->isFlashing()) {
            finish(id, operation, false, {}, QStringLiteral("busy")); return;
        }
        m_firmwareUpdater->checkForUpdate();
    } else if (operation == QLatin1String("startFirmware")) {
        if (!m_firmwareUpdater) { finish(id, operation, false, {}, QStringLiteral("unavailable")); return; }
        if (m_firmwareUpdater->isSimulated()) { finish(id, operation, false, {}, QStringLiteral("simulatedDevice")); return; }
        const auto state = m_firmwareUpdater->state();
        if (m_firmwareUpdater->availableVersion() <= 0) {
            finish(id, operation, false, {}, QStringLiteral("unavailable")); return;
        }
        if (state == FirmwareUpdater::State::Checking || state == FirmwareUpdater::State::Downloading
            || m_firmwareUpdater->isFlashing()) {
            finish(id, operation, false, {}, QStringLiteral("busy")); return;
        }
        m_firmwareUpdater->startUpdate();
    } else if (operation == QLatin1String("retryFirmware")) {
        if (!m_firmwareUpdater || !m_firmwareUpdater->retryAvailable()) {
            finish(id, operation, false, {}, QStringLiteral("unavailable")); return;
        }
        m_firmwareUpdater->retry();
    } else if (operation == QLatin1String("dismissFirmwareAvailability")) {
        if (!m_firmwareUpdater) { finish(id, operation, false, {}, QStringLiteral("unavailable")); return; }
        m_firmwareUpdater->dismissAvailability();
    } else if (operation == QLatin1String("setTranslationUploadEnabled")) {
        if (!m_settingsApp) { finish(id, operation, false, {}, QStringLiteral("unavailable")); return; }
        m_settingsApp->setDeveloperTranslationUpload(args.value(QStringLiteral("enabled")).toBool());
    } else {
        finish(id, operation, false, {}, QStringLiteral("unsupportedOperation")); return;
    }
    finish(id, operation, true, {{QStringLiteral("accepted"), true}});
}
