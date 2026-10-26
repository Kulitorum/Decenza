#include "shotreviewactionssource.h"
#include "network/remoteaccess.h"

#include "controllers/maincontroller.h"
#include "network/decentshotuploader.h"
#include "network/shotuploads.h"
#include "network/visualizeruploader.h"
#include "network/visualizershotsync.h"
#include "core/settings.h"
#include "core/settings_visualizer.h"
#include "core/settings_decent.h"
#include "core/settings_network.h"
#include "core/settings_mcp.h"
#include "ai/aimanager.h"
#include "ble/blemanager.h"
#include "ble/refractometers/refractometerproxy.h"
#include "core/dbutils.h"
#include "history/shothistorystorage.h"
#include "history/shotfileexport.h"
#include "opaquefilebundlestore.h"
#include "filebundlepolicy.h"
#include "servicevalue.h"

#include <QRegularExpression>
#include <QCoreApplication>
#include <QFile>
#include <QRunnable>
#include <QThreadPool>

namespace {
QString uploadResultCode(DecentShotUploader::Result result)
{
    using Result = DecentShotUploader::Result;
    switch (result) {
    case Result::None: return QStringLiteral("none");
    case Result::Uploaded: return QStringLiteral("uploaded");
    case Result::NotReplaced: return QStringLiteral("notReplaced");
    case Result::Maintenance: return QStringLiteral("maintenance");
    case Result::TooShort: return QStringLiteral("tooShort");
    case Result::NotLinked: return QStringLiteral("notLinked");
    case Result::NoMachine: return QStringLiteral("noMachine");
    case Result::NoSerial: return QStringLiteral("noSerial");
    case Result::NotFound: return QStringLiteral("notFound");
    case Result::Rejected: return QStringLiteral("rejected");
    case Result::NeedsSignIn: return QStringLiteral("needsSignIn");
    case Result::NotRegistered: return QStringLiteral("notRegistered");
    case Result::Failed: return QStringLiteral("failed");
    }
    return QStringLiteral("none");
}
} // namespace

NativeShotReviewActionSource::NativeShotReviewActionSource(MainController* controller, Settings* settings,
                                                           BLEManager* bleManager,
                                                           RefractometerProxy* refractometer,
                                                           QObject* parent)
    : ShotReviewActionStateSimpleSource(parent), m_controller(controller), m_settings(settings),
      m_bleManager(bleManager), m_refractometer(refractometer)
{
    setDecentUploadedResult(static_cast<int>(DecentShotUploader::Result::Uploaded));
    if (settings) {
        connect(settings, &Settings::savedRefractometerChanged,
                this, &NativeShotReviewActionSource::publishRefractometerState);
        if (settings->visualizer())
            connect(settings->visualizer(), &SettingsVisualizer::visualizerActiveChanged,
                    this, &NativeShotReviewActionSource::publishState);
        if (settings->decent())
            connect(settings->decent(), &SettingsDecent::activeChanged,
                    this, &NativeShotReviewActionSource::publishState);
        if (settings->network()) {
            connect(settings->network(), &SettingsNetwork::discussShotAppChanged,
                    this, &NativeShotReviewActionSource::publishState);
            connect(settings->network(), &SettingsNetwork::claudeRcSessionUrlChanged,
                    this, &NativeShotReviewActionSource::publishState);
            connect(settings->network(), &SettingsNetwork::shotServerEnabledChanged,
                    this, &NativeShotReviewActionSource::publishState);
        }
        if (settings->mcp()) {
            connect(settings->mcp(), &SettingsMcp::mcpEnabledChanged,
                    this, &NativeShotReviewActionSource::publishState);
        }
    }
    if (!controller) {
        publishState();
        publishRefractometerState();
        return;
    }
    if (bleManager) {
        connect(bleManager, &BLEManager::refractometerConnectedChanged,
                this, &NativeShotReviewActionSource::publishRefractometerState);
        connect(bleManager, &BLEManager::refractometerHuntChanged,
                this, &NativeShotReviewActionSource::publishRefractometerState);
    }
    if (refractometer) {
        connect(refractometer, &RefractometerProxy::measurementStarted, this, [this] {
            ++m_measurementGeneration;
            m_sessions.noteMeasurementStarted();
            publishRefractometerState();
        });
        connect(refractometer, &RefractometerProxy::measurementFinished,
                this, &NativeShotReviewActionSource::completeMeasurement);
        connect(refractometer, &RefractometerProxy::connectedChanged,
                this, &NativeShotReviewActionSource::publishRefractometerState);
        connect(refractometer, &RefractometerProxy::measuringChanged,
                this, [this] {
            if (m_refractometer && m_refractometer->measuring()) {
                ++m_measurementGeneration;
                m_sessions.noteMeasurementStarted();
            }
            publishRefractometerState();
            if (m_refractometer && !m_refractometer->measuring()) {
                const quint64 generation = m_measurementGeneration;
                QMetaObject::invokeMethod(this, [this, generation] {
                    if (generation == m_measurementGeneration) completeMeasurement();
                }, Qt::QueuedConnection);
            }
        });
        connect(refractometer, &RefractometerProxy::tdsChanged,
                this, &NativeShotReviewActionSource::publishRefractometerState);
        connect(refractometer, &RefractometerProxy::measurementReading,
                this, [this](double tds) {
            m_sessions.noteMeasurementStarted();
            publishRefractometerState();
            emit refractometerReading(m_sessions.measurementOwner(), tds);
        });
        connect(refractometer, &RefractometerProxy::temperatureChanged,
                this, &NativeShotReviewActionSource::publishRefractometerState);
        connect(refractometer, &RefractometerProxy::nameChanged,
                this, &NativeShotReviewActionSource::publishRefractometerState);
        connect(refractometer, &RefractometerProxy::measurementComplete,
                this, &NativeShotReviewActionSource::completeMeasurement);
        connect(refractometer, &RefractometerProxy::averageProgress,
                this, [this](int completed, int total) {
            m_sessions.noteMeasurementStarted();
            emit refractometerAverageProgress(m_sessions.measurementOwner(), completed, total);
        });
    }
    if (controller->aiManager()) {
        connect(controller->aiManager(), &AIManager::configurationChanged,
                this, &NativeShotReviewActionSource::publishState);
        connect(controller->aiManager(), &AIManager::analyzingChanged,
                this, &NativeShotReviewActionSource::publishState);
    }
    if (controller->visualizer()) {
        connect(controller->visualizer(), &VisualizerUploader::uploadingChanged,
                this, &NativeShotReviewActionSource::publishUploadState);
        connect(controller->visualizer(), &VisualizerUploader::lastUploadStatusChanged,
                this, &NativeShotReviewActionSource::publishUploadState);
        connect(controller->visualizer(), &VisualizerUploader::uploadSucceededForShot,
                this, &ShotReviewActionStateSource::visualizerUploadSucceeded);
        connect(controller->visualizer(), &VisualizerUploader::savedShotFinished,
                this, &ShotReviewActionStateSource::savedShotFinished);
    }
    if (controller->decentUploader()) {
        connect(controller->decentUploader(), &DecentShotUploader::uploadingChanged,
                this, &NativeShotReviewActionSource::publishUploadState);
        connect(controller->decentUploader(), &DecentShotUploader::uploadFinished, this,
                [this](qint64 shotId, DecentShotUploader::Result result) {
                    emit decentUploadFinished(shotId, static_cast<int>(result));
                    publishUploadState();
                });
    }
    publishState();
    publishRefractometerState();
}

void NativeShotReviewActionSource::publishRefractometerState()
{
    setRefractometerOwnerReviewId(m_sessions.measurementOwner());
    setRefractometerConfigured(m_settings && !m_settings->savedRefractometerAddress().isEmpty());
    setRefractometerConnected(m_bleManager && m_bleManager->isRefractometerConnected());
    setRefractometerMeasuring(m_sessions.measuring()
        || (m_refractometer && m_refractometer->measuring()));
    setRefractometerTds(m_refractometer ? m_refractometer->tds() : 0.0);
    setRefractometerTemperature(m_refractometer ? m_refractometer->temperature() : 0.0);
    setRefractometerName(m_refractometer ? m_refractometer->name() : QString());
    setSavedRefractometerAddress(m_settings ? m_settings->savedRefractometerAddress() : QString());
    setSavedRefractometerName(m_settings ? m_settings->savedRefractometerName() : QString());
}

void NativeShotReviewActionSource::openClient(const QString& clientId, bool native)
{
    m_sessions.openClient(clientId, native);
}

void NativeShotReviewActionSource::clearExport(const QString& reviewId)
{
    delete m_exportFiles.take(reviewId).files.data();
}

void NativeShotReviewActionSource::prepareExport(const QString& requestId, const QString& clientId,
                                                const QString& reviewId, qint64 shotId, ShotFileExport::Format format)
{
    const QString operation = QStringLiteral("prepareShotDownload");
    auto* history = m_controller->shotHistory();
    if (!history || !history->isReady() || !QCoreApplication::instance()) {
        finish(requestId, operation, false, {}, QStringLiteral("Shot history is unavailable."));
        return;
    }
    const auto previous = m_exportFiles.value(reviewId);
    if (!previous.requestId.isEmpty() || (previous.files && previous.files->hasBundle())) {
        finish(requestId, operation, false, {}, QStringLiteral("A shot download is already in progress."));
        return;
    }
    clearExport(reviewId);
    auto* files = new OpaqueFileBundleStore(this);
    m_exportFiles.insert(reviewId, {clientId, requestId, files});
    const QPointer<OpaqueFileBundleStore> fileGuard(files);
    const QPointer<NativeShotReviewActionSource> self(this);
    const QString dbPath = history->databasePath();
    QObject* context = QCoreApplication::instance();
    QThreadPool::globalInstance()->start(QRunnable::create(
        [self, fileGuard, context, dbPath, requestId, reviewId, shotId, format] {
        QString reason;
        ShotRecord record;
        if (!withTempDb(dbPath, "shot_download", [&](QSqlDatabase& db) {
                record = ShotHistoryStorage::loadShotRecordStatic(db, shotId, nullptr, Q_FUNC_INFO);
            }))
            reason = QStringLiteral("Shot history is unavailable.");
        const QByteArray payload = reason.isEmpty() ? ShotFileExport::data(record, format, &reason)
                                                   : QByteArray();
        auto directory = std::make_shared<QTemporaryDir>();
        const bool log = format == ShotFileExport::Format::DebugLog;
        const QString name = log ? QStringLiteral("shot-%1-debug.log").arg(shotId)
            : QStringLiteral("shot-%1%2.json").arg(shotId)
                .arg(format == ShotFileExport::Format::Profile ? QStringLiteral("-profile") : QString());
        const QString bundleId = ServiceOperation::newId().toLower();
        const QString fileName = bundleId + (log ? QStringLiteral(".log") : QStringLiteral(".json"));
        if (reason.isEmpty()) {
            QFile file(directory->filePath(fileName));
            if (!directory->isValid() || !file.open(QIODevice::WriteOnly | QIODevice::NewOnly)
                || payload.isEmpty() || file.write(payload) != payload.size() || !file.flush())
                reason = QStringLiteral("The shot file could not be created.");
        }
        QMetaObject::invokeMethod(context,
            [self, fileGuard, directory, bundleId, fileName, name, log, requestId, reviewId, reason]() mutable {
            if (!self) return;
            auto current = self->m_exportFiles.find(reviewId);
            if (!fileGuard || current == self->m_exportFiles.end()
                || current->files != fileGuard || current->requestId != requestId) {
                self->finish(requestId, QStringLiteral("prepareShotDownload"), false, {},
                             QStringLiteral("The shot download was cancelled."));
                return;
            }
            current->requestId.clear();
            QVariantMap result;
            if (reason.isEmpty()) {
                result = fileGuard->adopt(directory, bundleId, fileName, name,
                    log ? FileBundlePolicy::MaximumLogBytes : ServiceValue::MaximumExactInteger,
                    log ? QStringLiteral("text/plain") : QStringLiteral("application/json"),
                    log ? QStringLiteral("Log files (*.log)") : QStringLiteral("JSON files (*.json)"));
                if (result.isEmpty()) reason = QStringLiteral("The shot file could not be opened.");
            }
            if (!reason.isEmpty()) self->clearExport(reviewId);
            self->finish(requestId, QStringLiteral("prepareShotDownload"), reason.isEmpty(), result, reason);
        }, Qt::QueuedConnection);
    }));
}

void NativeShotReviewActionSource::closeClient(const QString& clientId)
{
    const auto exports = m_exportFiles.keys();
    for (const QString& reviewId : exports) {
        if (m_exportFiles.value(reviewId).clientId == clientId)
            clearExport(reviewId);
    }
    const auto released = m_sessions.closeClient(clientId);
    if (m_controller && m_controller->shotUploads()) {
        for (qint64 shotId : released)
            m_controller->shotUploads()->releaseUpdates(shotId);
    }
    synchronizeReviewSessions();
}

void NativeShotReviewActionSource::synchronizeReviewSessions()
{
    if (m_bleManager)
        m_bleManager->setRefractometerHunt(m_sessions.hasActiveReviews());
    if (!m_sessions.hasActiveReviews() && !m_sessions.measuring() && m_refractometer)
        m_refractometer->disconnectFromDevice();
    publishRefractometerState();
}

void NativeShotReviewActionSource::completeMeasurement()
{
    if (!m_sessions.measuring()) return;
    ++m_measurementGeneration;
    emit refractometerMeasurementComplete(m_sessions.measurementOwner());
    m_sessions.finishMeasurement();
    synchronizeReviewSessions();
}

bool NativeShotReviewActionSource::validRequestId(const QString& requestId) const
{
    static const QRegularExpression uuid(
        QStringLiteral("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$"));
    return requestId.size() == 36 && uuid.match(requestId).hasMatch();
}

void NativeShotReviewActionSource::publishUploadState()
{
    if (!m_controller)
        return;
    const bool visualizer = m_controller->visualizer() && m_controller->visualizer()->isUploading();
    const bool decent = m_controller->decentUploader() && m_controller->decentUploader()->uploading();
    const QString status = m_controller->visualizer()
        ? m_controller->visualizer()->lastUploadStatus() : QString();
    if (visualizerUploading() != visualizer) {
        setVisualizerUploading(visualizer);
    }
    if (decentUploading() != decent) {
        setDecentUploading(decent);
    }
    if (m_controller->decentUploader()) {
        const auto* uploader = m_controller->decentUploader();
        setDecentLastShotId(uploader->lastShotId());
        setDecentLastHttpStatus(uploader->lastHttpStatus());
        setDecentLastSerial(uploader->lastSerial());
        setDecentResultCode(uploadResultCode(uploader->lastResult()));
    } else {
        setDecentLastShotId(0);
        setDecentLastHttpStatus(0);
        setDecentLastSerial({});
        setDecentResultCode(QStringLiteral("none"));
    }
    if (visualizerLastUploadStatus() != status) {
        setVisualizerLastUploadStatus(status);
    }
}

void NativeShotReviewActionSource::setRemoteAccess(RemoteAccess* remoteAccess)
{
    if (m_remoteAccess)
        disconnect(m_remoteAccess, nullptr, this, nullptr);
    m_remoteAccess = remoteAccess;
    if (m_remoteAccess)
        connect(m_remoteAccess, &RemoteAccess::connectorUrlChanged,
                this, &NativeShotReviewActionSource::publishState);
    publishState();
}

void NativeShotReviewActionSource::publishState()
{
    if (m_settings) {
        setVisualizerActive(m_settings->visualizer() && m_settings->visualizer()->visualizerActive());
        setDecentActive(m_settings->decent() && m_settings->decent()->active());
        setDiscussShotUrl(m_settings->network() ? m_settings->network()->discussShotUrl() : QString());
        const auto* network = m_settings->network();
        setDiscussEnabled(network && network->discussShotApp() != network->discussAppNone());
        setDiscussReady(network && (network->discussShotApp() != network->discussAppClaudeDesktop()
            || !network->claudeRcSessionUrl().isEmpty()));
        if (network && m_settings->mcp()) {
            const bool localMcpServing = m_settings->mcp()->mcpEnabled() && network->shotServerEnabled();
            const bool remoteConnectorLive = m_remoteAccess && !m_remoteAccess->connectorUrl().isEmpty();
            setDiscussAppReadsShot(SettingsNetwork::discussAppReadsShot(
                network->discussShotApp(), localMcpServing, remoteConnectorLive));
        }
    }
    if (m_controller && m_controller->aiManager()) {
        setAiConfigured(m_controller->aiManager()->isConfigured());
        setAiAnalyzing(m_controller->aiManager()->isAnalyzing());
    }
    publishUploadState();
    publishRefractometerState();
}

void NativeShotReviewActionSource::finish(const QString& requestId, const QString& operation,
                                          bool success, const QVariantMap& result,
                                          const QString& reason)
{
    m_requests.finish(requestId);
    emit actionCompleted(requestId, operation, success, result, reason);
}

void NativeShotReviewActionSource::execute(QString requestId, QString operation,
                                           QVariantMap arguments)
{
    if (!validRequestId(requestId))
        return;
    const auto start = m_requests.begin(requestId);
    if (start == ServiceRequestIds::Start::DuplicateId)
        return;
    if (start != ServiceRequestIds::Start::Accepted) {
        emit actionCompleted(requestId, operation, false, {},
                             QStringLiteral("Too many review actions are pending."));
        return;
    }
    const QString requestReviewId = arguments.value(QStringLiteral("reviewId")).toString();
    auto fail = [this, &requestId, &operation, &requestReviewId](const QString& reason) {
        finish(requestId, operation, false,
               {{QStringLiteral("reviewId"), requestReviewId}}, reason);
    };
    const QString clientId = arguments.take(QStringLiteral("clientId")).toString();
    if (!m_controller) {
        fail(QStringLiteral("Review actions are unavailable."));
        return;
    }

    if (operation == QLatin1String("refractometerHunt")
        || operation == QLatin1String("scanForDevices")
        || operation == QLatin1String("requestRefractometerMeasurement")) {
        if (!m_bleManager || !m_refractometer) {
            fail(QStringLiteral("Refractometer controls are unavailable."));
            return;
        }
        const QString reviewId = arguments.take(QStringLiteral("reviewId")).toString();
        if (!m_sessions.owns(clientId, reviewId)) {
            fail(QStringLiteral("The shot review is no longer active."));
            return;
        }
        if (operation == QLatin1String("refractometerHunt")) {
            const QVariant active = arguments.value(QStringLiteral("active"));
            if (arguments.size() != 1 || active.metaType().id() != QMetaType::Bool) {
                fail(QStringLiteral("Invalid refractometer hunt request."));
                return;
            }
            m_sessions.setActive(clientId, reviewId, active.toBool());
            synchronizeReviewSessions();
        } else {
            if (!arguments.isEmpty()) {
                fail(QStringLiteral("Invalid refractometer request."));
                return;
            }
            if (operation == QLatin1String("scanForDevices"))
                m_bleManager->scanForDevices();
            else if (m_sessions.measuring() || !m_refractometer->connected()
                     || !m_sessions.beginMeasurement(clientId, reviewId)) {
                fail(QStringLiteral("The refractometer is unavailable or another review is measuring."));
                return;
            } else {
                publishRefractometerState();
                m_refractometer->requestMeasurement();
            }
        }
        publishRefractometerState();
        finish(requestId, operation, true, {{QStringLiteral("reviewId"), reviewId}});
        return;
    }

    auto readShotId = [&arguments](qint64* shotId) {
        const QVariant value = arguments.value(QStringLiteral("shotId"));
        const int type = value.metaType().id();
        if (type == QMetaType::Bool || (type != QMetaType::Int && type != QMetaType::UInt
            && type != QMetaType::LongLong && type != QMetaType::ULongLong))
            return false;
        bool ok = false;
        const qlonglong id = value.toLongLong(&ok);
        if (!ok || id <= 0 || id > 9007199254740991LL)
            return false;
        *shotId = id;
        return true;
    };

    qint64 shotId = 0;
    if (operation == QLatin1String("prepareShotDownload")
        || operation == QLatin1String("readBundleChunk")
        || operation == QLatin1String("releaseBundle")) {
        const QString reviewId = arguments.take(QStringLiteral("reviewId")).toString();
        if (!m_sessions.owns(clientId, reviewId)) {
            fail(QStringLiteral("The shot review is no longer active."));
            return;
        }
        if (operation == QLatin1String("prepareShotDownload")) {
            const QString format = arguments.value(QStringLiteral("format")).toString();
            if (arguments.size() != 2 || !readShotId(&shotId)
                || !m_sessions.owns(clientId, reviewId, shotId)
                || (format != QLatin1String("shot") && format != QLatin1String("profile")
                    && format != QLatin1String("debugLog"))) {
                fail(QStringLiteral("Invalid shot export request."));
                return;
            }
            prepareExport(requestId, clientId, reviewId, shotId,
                format == QLatin1String("debugLog") ? ShotFileExport::Format::DebugLog
                : format == QLatin1String("profile") ? ShotFileExport::Format::Profile
                                                   : ShotFileExport::Format::Shot);
            return;
        }
        const auto entry = m_exportFiles.value(reviewId);
        if (entry.clientId != clientId || !entry.files || !entry.requestId.isEmpty()) {
            fail(QStringLiteral("The shot export is unavailable."));
            return;
        }
        QString reason;
        if (operation == QLatin1String("releaseBundle")) {
            const bool released = entry.files->release(arguments, &reason);
            if (released) clearExport(reviewId);
            finish(requestId, operation, released, {}, reason);
        } else {
            const QVariantMap result = entry.files->readChunk(arguments, &reason);
            finish(requestId, operation, reason.isEmpty(), result, reason);
        }
        return;
    }
    if (operation == QLatin1String("hold") || operation == QLatin1String("expectHeldEdit")
        || operation == QLatin1String("release")) {
        const QString reviewId = arguments.take(QStringLiteral("reviewId")).toString();
        if (arguments.size() != 1 || !readShotId(&shotId) || !m_controller->shotUploads()) {
            fail(QStringLiteral("Invalid review lease."));
            return;
        }
        auto* uploads = m_controller->shotUploads();
        if (operation == QLatin1String("hold")) {
            const auto held = m_sessions.hold(clientId, reviewId, shotId);
            if (held == ShotReviewSessions::Hold::Rejected) {
                fail(QStringLiteral("The review client is disconnected or its lease limit was reached."));
                return;
            }
            if (held == ShotReviewSessions::Hold::FirstForShot) uploads->holdUpdates(shotId);
        } else {
            if (!m_sessions.owns(clientId, reviewId, shotId)) {
                fail(QStringLiteral("The review lease has ended."));
                return;
            }
            if (operation == QLatin1String("expectHeldEdit")) uploads->expectHeldEdit(shotId);
            else {
                clearExport(reviewId);
                if (m_sessions.release(clientId, reviewId) > 0) uploads->releaseUpdates(shotId);
                synchronizeReviewSessions();
            }
        }
        finish(requestId, operation, true, {{QStringLiteral("reviewId"), reviewId}});
        return;
    }
    if (operation == QLatin1String("upload")
        || operation == QLatin1String("refreshVisualizer")) {
        if (arguments.size() != 1 || !readShotId(&shotId)) {
            fail(QStringLiteral("Invalid shot request."));
            return;
        }
        if (operation == QLatin1String("refreshVisualizer")) {
            if (!m_controller->visualizerSync()) {
                fail(QStringLiteral("Visualizer sync is unavailable."));
                return;
            }
            m_controller->visualizerSync()->refreshShot(shotId);
        } else {
            auto* uploads = m_controller->shotUploads();
            if (!uploads) {
                fail(QStringLiteral("Shot uploads are unavailable."));
                return;
            }
            uploads->uploadNow(shotId);
        }
        finish(requestId, operation, true);
        return;
    }

    if (operation == QLatin1String("prose")) {
        const QVariantMap shot = arguments.value(QStringLiteral("shot")).toMap();
        if (arguments.size() != 1 || shot.isEmpty() || shot.size() > 256) {
            fail(QStringLiteral("Invalid shot analysis request."));
            return;
        }
        auto* ai = m_controller->aiManager();
        if (!ai) {
            fail(QStringLiteral("Shot analysis is unavailable."));
            return;
        }
        const QString prose = ai->buildShotAnalysisProseForShot(shot);
        if (prose.isEmpty()) {
            fail(QStringLiteral("Shot analysis could not be prepared."));
            return;
        }
        finish(requestId, operation, true, {{QStringLiteral("text"), prose}});
        return;
    }

    fail(QStringLiteral("Unsupported review action."));
}
