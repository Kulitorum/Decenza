#include "updateservicesource.h"
#include "updateoperationpolicy.h"

#include "ble/de1device.h"
#include "controllers/firmwareupdater.h"
#include "core/settings_app.h"
#include "core/updatechecker.h"
#include "importservicesource.h"
#include "network/shotserveruploadroute.h"

#include <QMetaType>
#include <QDir>
#include <QFileInfo>
#include <QThread>
#include <QTemporaryDir>

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
        connect(m_firmwareUpdater, &FirmwareUpdater::stateChanged, this, &NativeUpdateServiceSource::publish);
        connect(m_firmwareUpdater, &FirmwareUpdater::availabilityChanged, this, &NativeUpdateServiceSource::publish);
        connect(m_firmwareUpdater, &FirmwareUpdater::progressChanged, this, &NativeUpdateServiceSource::publish);
        connect(m_firmwareUpdater, &FirmwareUpdater::installedVersionChanged, this, &NativeUpdateServiceSource::publish);
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

void NativeUpdateServiceSource::setImportService(NativeImportServiceSource* imports)
{
    m_imports = imports;
}

void NativeUpdateServiceSource::setInstallAllowed(std::function<bool()> allowed)
{
    m_installAllowed = std::move(allowed);
    if (m_appUpdater) {
        const QPointer<FirmwareUpdater> firmware = m_firmwareUpdater;
        m_appUpdater->setInstallAllowed([installationAllowed = m_installAllowed, firmware] {
            return installationAllowed && installationAllowed() && (!firmware || (!firmware->isFlashing()
                && firmware->state() != FirmwareUpdater::State::Downloading));
        });
    }
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
            {QStringLiteral("installing"), m_appUpdater->isInstalling()},
            {QStringLiteral("canInstallLocalApk"), m_appUpdater->canInstallLocalApk()},
            {QStringLiteral("preparingLocalApk"), !m_apkRequest.isEmpty()}
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
            {QStringLiteral("installedVersion"), m_firmwareUpdater->installedVersion()},
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
    const QPointer<NativeUpdateServiceSource> guard(this);
    setSnapshot(capture());
    if (!guard) return;
    setReady(available);
}

void NativeUpdateServiceSource::refresh() { publish(); }

void NativeUpdateServiceSource::finish(const QString& id, const QString& operation,
                                       bool success, const QVariantMap& result,
                                       const QString& reason)
{
    if (!m_requests.finish(id)) return;
    const QPointer<NativeUpdateServiceSource> guard(this);
    publish();
    if (!guard) return;
    emit operationCompleted(id, operation, success, result, reason);
}

void NativeUpdateServiceSource::execute(QString id, QString operation, QVariantMap args)
{
    const QPointer<NativeUpdateServiceSource> guard(this);
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
    if (operation == QLatin1String("installLocalApk")) {
        if (!m_appUpdater || !m_imports) { finish(id, operation, false, {}, QStringLiteral("unavailable")); return; }
        if (!m_appUpdater->canInstallLocalApk()) { finish(id, operation, false, {}, QStringLiteral("unsupportedPlatform")); return; }
        if (!m_apkRequest.isEmpty() || m_appUpdater->isChecking() || m_appUpdater->isDownloading()
            || m_appUpdater->isInstalling() || (m_firmwareUpdater && (m_firmwareUpdater->isFlashing()
                || m_firmwareUpdater->state() == FirmwareUpdater::State::Downloading))) {
            finish(id, operation, false, {}, QStringLiteral("busy")); return;
        }
        const auto allowed = m_installAllowed;
        const bool installAllowed = allowed && allowed();
        if (!guard) return;
        if (!installAllowed) { finish(id, operation, false, {}, QStringLiteral("machineBusy")); return; }
        auto transfer = m_imports->takeCompletedTransfer(args.value(QStringLiteral("transferId")).toString(), QStringLiteral("apkFile"));
        if (!guard) return;
        if (!transfer || !transfer->isValid()) { finish(id, operation, false, {}, QStringLiteral("invalidTransfer")); return; }
        auto upload = std::shared_ptr<QTemporaryDir>(std::move(transfer));
        auto path = std::make_shared<QString>();
        auto reason = std::make_shared<QString>();
        m_apkRequest = id;
        publish();
        if (!guard) return;
        auto* worker = QThread::create([upload, path, reason] {
            *path = UpdateOperationPolicy::validatedLocalApkPath(upload->path(), reason.get());
        });
        connect(worker, &QThread::finished, this, [this, id, operation, upload, path, reason] {
            if (m_apkRequest != id) return;
            const QPointer<NativeUpdateServiceSource> callbackGuard(this);
            QString failure = *reason;
            const QPointer<UpdateChecker> updater = m_appUpdater;
            if (failure.isEmpty() && (!updater || !updater->canInstallLocalApk())) failure = QStringLiteral("unavailable");
            if (failure.isEmpty() && (updater->isChecking() || updater->isDownloading() || updater->isInstalling())) failure = QStringLiteral("busy");
            if (failure.isEmpty() && m_firmwareUpdater && (m_firmwareUpdater->isFlashing()
                || m_firmwareUpdater->state() == FirmwareUpdater::State::Downloading)) failure = QStringLiteral("busy");
            const auto stillAllowed = m_installAllowed;
            if (failure.isEmpty() && (!stillAllowed || !stillAllowed())) failure = QStringLiteral("machineBusy");
            if (!callbackGuard) return;
            if (failure.isEmpty() && !updater) failure = QStringLiteral("unavailable");
            if (failure.isEmpty() && !updater->installLocalApk(*path, upload))
                failure = updater && !updater->errorMessage().isEmpty() ? updater->errorMessage() : QStringLiteral("installFailed");
            if (!callbackGuard) return;
            m_apkRequest.clear();
            m_apkWorker = nullptr;
            finish(id, operation, failure.isEmpty(), {{QStringLiteral("accepted"), failure.isEmpty()}}, failure);
        }, Qt::QueuedConnection);
        connect(worker, &QThread::finished, worker, &QObject::deleteLater);
        m_apkWorker = worker;
        worker->start();
        return;
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
        const auto allowed = m_installAllowed;
        const bool installAllowed = allowed && allowed();
        if (!guard) return;
        if (!installAllowed) { finish(id, operation, false, {}, QStringLiteral("machineBusy")); return; }
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
        if (m_firmwareUpdater->isSimulated()) { finish(id, operation, false, {}, QStringLiteral("simulatedDevice")); return; }
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
    if (!guard) return;
    if ((operation == QLatin1String("startFirmware") || operation == QLatin1String("retryFirmware"))
        && m_firmwareUpdater && m_firmwareUpdater->state() == FirmwareUpdater::State::Failed) {
        finish(id, operation, false, {}, m_firmwareUpdater->errorMessage());
        return;
    }
    finish(id, operation, true, {{QStringLiteral("accepted"), true}});
}
