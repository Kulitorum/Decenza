#include "shotreviewactionssource.h"
#include "network/remoteaccess.h"

#include "controllers/maincontroller.h"
#include "network/decentshotuploader.h"
#include "network/shotuploads.h"
#include "network/visualizeruploader.h"
#include "network/visualizershotsync.h"
#include "core/settings.h"
#include "core/settings_brew.h"
#include "core/settings_dye.h"
#include "core/settings_visualizer.h"
#include "core/settings_decent.h"
#include "core/settings_network.h"
#include "core/settings_mcp.h"
#include "ai/aimanager.h"
#include "ble/blemanager.h"
#include "ble/refractometers/refractometerproxy.h"
#include "core/dbutils.h"
#include "core/shotreviewproposal.h"
#include "core/shotreviewpersistentplan.h"
#include "history/shotreviewcommit.h"
#include "history/shothistorystorage.h"
#include "history/shotfileexport.h"
#include "opaquefilebundlestore.h"
#include "filebundlepolicy.h"
#include "servicevalue.h"

#include <QRegularExpression>
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
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
        const auto setupChanged = [this] { invalidateCarryProposals(); };
        auto* dye = settings->dye();
        connect(dye, &SettingsDye::dyeBeanBrandChanged, this, setupChanged);
        connect(dye, &SettingsDye::dyeBeanTypeChanged, this, setupChanged);
        connect(dye, &SettingsDye::dyeRoastDateChanged, this, setupChanged);
        connect(dye, &SettingsDye::dyeRoastLevelChanged, this, setupChanged);
        connect(dye, &SettingsDye::dyeBeanBaseIdChanged, this, setupChanged);
        connect(dye, &SettingsDye::dyeBeanBaseDataChanged, this, setupChanged);
        connect(dye, &SettingsDye::dyeBeanWeightChanged, this, setupChanged);
        connect(dye, &SettingsDye::dyeGrinderSettingChanged, this, setupChanged);
        connect(dye, &SettingsDye::dyeGrinderRpmChanged, this, setupChanged);
        connect(dye, &SettingsDye::dyeBaristaChanged, this, setupChanged);
        connect(dye, &SettingsDye::activeBagIdChanged, this, setupChanged);
        connect(dye, &SettingsDye::activeRecipeIdChanged, this, setupChanged);
        connect(dye, &SettingsDye::activeEquipmentIdChanged, this, setupChanged);
        connect(settings->brew(), &SettingsBrew::brewOverridesChanged, this, setupChanged);
        connect(settings->brew(), &SettingsBrew::temperatureOverrideChanged, this, setupChanged);
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
    connect(controller, &MainController::lastSavedShotIdChanged, this,
            [this] { invalidateCarryProposals(); });
    connect(controller, &MainController::reviewOperationGenerationChanged, this,
            [this] { invalidateCarryProposals(true); });
    connect(controller, &MainController::activeRecipeChanged, this,
            [this] { invalidateCarryProposals(true); });
    if (controller->profileManager()) {
        connect(controller->profileManager(), &ProfileManager::currentProfileChanged, this,
                [this] { invalidateCarryProposals(); });
        connect(controller->profileManager(), &ProfileManager::profileModifiedChanged, this,
                [this] { invalidateCarryProposals(); });
        connect(controller->profileManager(), &ProfileManager::targetWeightChanged, this,
                [this] { invalidateCarryProposals(); });
    }
    if (controller->shotHistory()) {
        connect(controller->shotHistory(), &ShotHistoryStorage::shotRequestReady,
                this, &NativeShotReviewActionSource::finishCarryPreview);
        connect(controller->shotHistory(), &ShotHistoryStorage::shotDeleted,
                this, &NativeShotReviewActionSource::invalidateCarryForShot);
        connect(controller->shotHistory(), &ShotHistoryStorage::shotMetadataUpdated,
                this, [this](qint64 shotId, bool success) {
            if (success) invalidateCarryForShot(shotId);
        });
        connect(controller->shotHistory(), &ShotHistoryStorage::shotMetadataUpdateRequestFinished,
                this, [this](const QString& id, qint64 shotId, bool success, const QString& reason) {
            const auto it = m_pendingReviewShotWrites.find(id);
            if (it == m_pendingReviewShotWrites.end() || it.value() != shotId) return;
            m_pendingReviewShotWrites.erase(it);
            finish(id, QStringLiteral("updateReviewShot"), success,
                   {{QStringLiteral("shotId"), shotId}}, reason);
        });
    }
    if (controller->bagStorage()) {
        connect(controller->bagStorage(), &CoffeeBagStorage::bagUpdateCommitted, this,
                [this](qint64 id, const QVariantMap&) { invalidateCarryForRecord(QStringLiteral("bag"), id); });
        connect(controller->bagStorage(), &CoffeeBagStorage::bagRemoved, this,
                [this](qint64 id) { invalidateCarryForRecord(QStringLiteral("bag"), id); });
    }
    if (controller->recipeStorage()) {
        connect(controller->recipeStorage(), &RecipeStorage::recipeChanged, this,
                [this](qint64 id) { invalidateCarryForRecord(QStringLiteral("recipe"), id); });
        connect(controller->recipeStorage(), &RecipeStorage::recipeRemoved, this,
                [this](qint64 id) { invalidateCarryForRecord(QStringLiteral("recipe"), id); });
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

void NativeShotReviewActionSource::invalidateCarryForShot(qint64 shotId)
{
    for (auto& preview : m_pendingCarryPreviews)
        if (preview.shotId == shotId) preview.shotChanged = true;
    QStringList invalidated;
    for (auto it = m_reviewProposals.begin(); it != m_reviewProposals.end();) {
        const auto baseline = m_reviewBaselines.constFind(it.key());
        if (baseline != m_reviewBaselines.cend() && baseline->shotId == shotId) {
            invalidated.append(it.key());
            it = m_reviewProposals.erase(it);
        } else ++it;
    }
    for (const QString& reviewId : invalidated)
        emit carryForwardInvalidated(reviewId);
}

void NativeShotReviewActionSource::invalidateCarryForRecord(const QString& kind, qint64 id)
{
    if (id <= 0) return;
    for (auto& preview : m_pendingCarryPreviews) {
        const auto baseline = m_reviewBaselines.constFind(preview.reviewId);
        if (baseline == m_reviewBaselines.cend()) continue;
        const qint64 openedBag = preview.opening.value(QStringLiteral("bagId"), -1).toLongLong();
        const qint64 savedBag = preview.saved.value(QStringLiteral("bagId"), -1).toLongLong();
        const qint64 target = kind == QLatin1String("recipe") ? baseline->setup.brew.activeRecipeId
            : ((openedBag > 0 ? openedBag : -1) != (savedBag > 0 ? savedBag : -1)
               ? savedBag : baseline->setup.brew.activeBagId);
        if (target == id) preview.recordChanged = true;
    }
    QStringList invalidated;
    for (auto it = m_reviewProposals.begin(); it != m_reviewProposals.end();) {
        if (it->persistentPlan.value(kind + QStringLiteral("Id")).toLongLong() == id) {
            invalidated.append(it.key());
            it = m_reviewProposals.erase(it);
        } else ++it;
    }
    for (const QString& reviewId : invalidated) emit carryForwardInvalidated(reviewId);
}

void NativeShotReviewActionSource::invalidateCarryProposals(bool recordChanged)
{
    if (m_reviewProposals.isEmpty()) return;
    const auto current = m_controller ? m_controller->currentShotReviewSetup() : std::nullopt;
    QStringList invalidated;
    for (auto it = m_reviewProposals.begin(); it != m_reviewProposals.end();) {
        const auto baseline = m_reviewBaselines.constFind(it.key());
        if (recordChanged || !m_controller || !current || baseline == m_reviewBaselines.cend()
            || baseline->shotId != m_controller->lastSavedShotId()
            || baseline->operationGeneration != m_controller->reviewOperationGeneration()
            || !it->expected.matches(*current)) {
            const QString reviewId = it.key();
            it = m_reviewProposals.erase(it);
            invalidated.append(reviewId);
        } else {
            ++it;
        }
    }
    for (const QString& reviewId : invalidated)
        emit carryForwardInvalidated(reviewId);
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
    for (auto it = m_pendingCarryPreviews.begin(); it != m_pendingCarryPreviews.end();) {
        if (it->clientId == clientId) {
            m_requests.finish(it.key());
            it = m_pendingCarryPreviews.erase(it);
        } else ++it;
    }
    for (auto it = m_reviewBaselines.begin(); it != m_reviewBaselines.end();) {
        if (it->clientId == clientId) {
            m_reviewProposals.remove(it.key());
            m_latestCarryPreview.remove(it.key());
            it = m_reviewBaselines.erase(it);
        }
        else ++it;
    }
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

void NativeShotReviewActionSource::finishCarryPreview(const QString& requestId,
                                                       qint64 shotId, const QVariantMap& shot,
                                                       const QString& reason)
{
    const auto it = m_pendingCarryPreviews.find(requestId);
    if (it == m_pendingCarryPreviews.end()) return;
    const PendingCarryPreview request = it.value();
    m_pendingCarryPreviews.erase(it);
    const QString operation = QStringLiteral("previewCarryForward");
    const auto baseline = m_reviewBaselines.constFind(request.reviewId);
    const auto current = m_controller ? m_controller->currentShotReviewSetup() : std::nullopt;
    if (!reason.isEmpty()) {
        finish(requestId, operation, false, {{QStringLiteral("reviewId"), request.reviewId}}, reason);
        return;
    }
    if (!m_controller || shotId != request.shotId || shot.isEmpty()
        || m_latestCarryPreview.value(request.reviewId) != requestId
        || request.shotId != m_controller->lastSavedShotId()
        || !m_sessions.owns(request.clientId, request.reviewId, request.shotId)
        || baseline == m_reviewBaselines.cend() || !current
        || m_controller->reviewOperationInProgress()
        || m_controller->reviewHasNewerEspressoOperation()
        || baseline->operationGeneration != m_controller->reviewOperationGeneration()
        || !baseline->setup.matches(*current)) {
        finish(requestId, operation, true, {{QStringLiteral("reviewId"), request.reviewId},
                                           {QStringLiteral("eligible"), false}});
        return;
    }
    const QVariantMap proposed = shotReviewProposal(*current, request.opening, request.saved);
    if (!proposed.value(QStringLiteral("valid")).toBool()) {
        finish(requestId, operation, false, {{QStringLiteral("reviewId"), request.reviewId}},
               QStringLiteral("The shot corrections could not be reviewed."));
        return;
    }
    const QVariantMap patch = proposed.value(QStringLiteral("patch")).toMap();
    if (patch.isEmpty()) {
        finish(requestId, operation, true, {{QStringLiteral("reviewId"), request.reviewId},
                                           {QStringLiteral("eligible"), false}});
        return;
    }
    // The proposal must describe the saved shot, not an optimistic edit that
    // failed or a value overwritten by another review while the read ran.
    for (auto field = patch.cbegin(); field != patch.cend(); ++field) {
        const QString shotKey = field.key() == QLatin1String("doseWeight")
            ? QStringLiteral("doseWeightG") : field.key();
        QVariant savedValue = shot.value(shotKey);
        if (field.key() == QLatin1String("beanBaseId")) {
            const auto entry = QJsonDocument::fromJson(shot.value(QStringLiteral("beanBaseJson"))
                                                       .toString().toUtf8()).object();
            savedValue = entry.value(QStringLiteral("id")).toVariant().toString();
        } else if (!shot.contains(shotKey)) {
            if (field.key() == QLatin1String("bagId") || field.key() == QLatin1String("equipmentId"))
                savedValue = -1;
            else if (field.key() == QLatin1String("rpm")) savedValue = 0;
            else if (field.key() == QLatin1String("beanBaseJson")) savedValue = QString();
            else {
                finish(requestId, operation, false, {{QStringLiteral("reviewId"), request.reviewId}},
                       QStringLiteral("The saved shot is missing a correction."));
                return;
            }
        }
        const bool numeric = field.key() == QLatin1String("doseWeight")
            || field.key() == QLatin1String("rpm") || field.key() == QLatin1String("bagId")
            || field.key() == QLatin1String("equipmentId");
        const bool idField = field.key() == QLatin1String("bagId")
            || field.key() == QLatin1String("equipmentId");
        const double storedNumber = savedValue.toDouble();
        const double proposedNumber = field.value().toDouble();
        if ((numeric && !(idField && storedNumber <= 0 && proposedNumber <= 0)
             && std::abs(storedNumber - proposedNumber) >= 0.005)
            || (!numeric && savedValue.toString() != field.value().toString())) {
            finish(requestId, operation, false, {{QStringLiteral("reviewId"), request.reviewId}},
                   QStringLiteral("The saved shot changed; review the correction again."));
            return;
        }
    }
    m_pendingCarryPreviews.insert(requestId, request);
    const qint64 bagId = patch.contains(QStringLiteral("bagId"))
        ? patch.value(QStringLiteral("bagId")).toLongLong() : current->brew.activeBagId;
    if (bagId <= 0) {
        requestCarryRecipe(requestId, proposed, *current, {});
    } else if (!m_controller->bagStorage()) {
        failCarryPreview(requestId, QStringLiteral("The selected bag is unavailable."));
    } else {
        m_controller->bagStorage()->requestBagWithCompletion(bagId, this,
            [this, requestId, proposed, expected = *current](qint64, bool success,
                                                              const QVariantMap& bag, const QString& readReason) {
                if (!m_pendingCarryPreviews.contains(requestId)) return;
                if (!success || bag.isEmpty()) {
                    failCarryPreview(requestId, readReason.isEmpty() ? QStringLiteral("The selected bag is unavailable.") : readReason);
                    return;
                }
                requestCarryRecipe(requestId, proposed, expected, bag);
            });
    }
}

void NativeShotReviewActionSource::failCarryPreview(const QString& requestId, const QString& reason)
{
    const auto it = m_pendingCarryPreviews.find(requestId);
    if (it == m_pendingCarryPreviews.end()) return;
    const QString reviewId = it->reviewId;
    m_pendingCarryPreviews.erase(it);
    finish(requestId, QStringLiteral("previewCarryForward"), false,
           {{QStringLiteral("reviewId"), reviewId}}, reason);
}

void NativeShotReviewActionSource::requestCarryRecipe(const QString& requestId,
                                                       const QVariantMap& proposed,
                                                       const ShotReviewSetup& expected,
                                                       const QVariantMap& bag)
{
    if (!m_pendingCarryPreviews.contains(requestId)) return;
    const qint64 recipeId = expected.brew.activeRecipeId;
    if (recipeId <= 0) {
        completeCarryPreview(requestId, proposed, expected, bag, {});
    } else if (!m_controller || !m_controller->recipeStorage()) {
        failCarryPreview(requestId, QStringLiteral("The active recipe is unavailable."));
    } else {
        m_controller->recipeStorage()->requestRecipeWithCompletion(recipeId, this,
            [this, requestId, proposed, expected, bag](qint64, bool success,
                                                       const QVariantMap& recipe, const QString& reason) {
                if (!m_pendingCarryPreviews.contains(requestId)) return;
                if (!success || recipe.isEmpty()) {
                    failCarryPreview(requestId, reason.isEmpty() ? QStringLiteral("The active recipe is unavailable.") : reason);
                    return;
                }
                completeCarryPreview(requestId, proposed, expected, bag, recipe);
            });
    }
}

void NativeShotReviewActionSource::completeCarryPreview(const QString& requestId,
                                                        const QVariantMap& proposed,
                                                        const ShotReviewSetup& expected,
                                                        const QVariantMap& bag,
                                                        const QVariantMap& recipe)
{
    const auto it = m_pendingCarryPreviews.find(requestId);
    if (it == m_pendingCarryPreviews.end()) return;
    const PendingCarryPreview request = it.value();
    m_pendingCarryPreviews.erase(it);
    const auto baseline = m_reviewBaselines.constFind(request.reviewId);
    const auto current = m_controller ? m_controller->currentShotReviewSetup() : std::nullopt;
    if (!m_controller || !current || m_latestCarryPreview.value(request.reviewId) != requestId
        || request.shotChanged || request.recordChanged
        || !m_sessions.owns(request.clientId, request.reviewId, request.shotId)
        || request.shotId != m_controller->lastSavedShotId()
        || baseline == m_reviewBaselines.cend() || m_controller->reviewOperationInProgress()
        || m_controller->reviewHasNewerEspressoOperation()
        || baseline->operationGeneration != m_controller->reviewOperationGeneration()
        || !baseline->setup.matches(*current) || !expected.matches(*current)) {
        finish(requestId, QStringLiteral("previewCarryForward"), true,
               {{QStringLiteral("reviewId"), request.reviewId}, {QStringLiteral("eligible"), false}});
        return;
    }
    const QVariantMap patch = proposed.value(QStringLiteral("patch")).toMap();
    const QVariantMap plan = shotReviewPersistentPlan(expected, patch, recipe, bag);
    if (!plan.value(QStringLiteral("valid")).toBool()) {
        finish(requestId, QStringLiteral("previewCarryForward"), false,
               {{QStringLiteral("reviewId"), request.reviewId}}, QStringLiteral("The selected records changed."));
        return;
    }
    m_reviewProposals.insert(request.reviewId, {requestId, patch, expected, plan, bag, recipe});
    QVariantMap result = proposed;
    result.insert(QStringLiteral("reviewId"), request.reviewId);
    result.insert(QStringLiteral("eligible"), true);
    result.insert(QStringLiteral("proposalId"), requestId);
    result.insert(QStringLiteral("persistentEffects"), plan.value(QStringLiteral("effects")));
    result.insert(QStringLiteral("recipeName"), recipe.value(QStringLiteral("name")));
    result.insert(QStringLiteral("bagName"), bag.value(QStringLiteral("coffeeName")));
    result.insert(QStringLiteral("bagRoaster"), bag.value(QStringLiteral("roasterName")));
    result.insert(QStringLiteral("previousBagName"), expected.beanType);
    result.insert(QStringLiteral("previousBagRoaster"), expected.beanBrand);
    finish(requestId, QStringLiteral("previewCarryForward"), true, result);
}

void NativeShotReviewActionSource::synchronizeReviewSessions()
{
    if (m_bleManager)
        m_bleManager->setRefractometerHunt(m_sessions.hasActiveReviews());
    if (!m_sessions.hasActiveReviews() && !m_sessions.measuring() && m_refractometer)
        m_refractometer->disconnectFromDevice();
    publishRefractometerState();
}

bool NativeShotReviewActionSource::carryApplyCurrent(const QString& clientId, const QString& reviewId,
                                                     const QString& proposalId, qint64 shotId,
                                                     const ShotReviewSetup& expected, quint64 operation) const
{
    const auto current = m_controller ? m_controller->currentShotReviewSetup() : std::nullopt;
    const auto proposal = m_reviewProposals.constFind(reviewId);
    return proposal != m_reviewProposals.cend() && proposal->requestId == proposalId
        && m_controller && current && m_sessions.owns(clientId, reviewId, shotId)
        && m_controller->lastSavedShotId() == shotId && !m_controller->reviewOperationInProgress()
        && !m_controller->reviewHasNewerEspressoOperation()
        && m_controller->reviewOperationGeneration() == operation && expected.matches(*current);
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
            if (held != ShotReviewSessions::Hold::Existing) {
                // The fallback is captured once per review, not on each later
                // eligibility check, so edits made while the page is open count.
                if (const auto baseline = m_controller->shotReviewBaseline(shotId))
                    m_reviewBaselines.insert(reviewId, {clientId, shotId, *baseline,
                                                       m_controller->reviewOperationGeneration()});
            }
            if (held == ShotReviewSessions::Hold::FirstForShot) uploads->holdUpdates(shotId);
        } else {
            if (!m_sessions.owns(clientId, reviewId, shotId)) {
                fail(QStringLiteral("The review lease has ended."));
                return;
            }
            if (operation == QLatin1String("expectHeldEdit")) uploads->expectHeldEdit(shotId);
            else {
                m_reviewProposals.remove(reviewId);
                m_latestCarryPreview.remove(reviewId);
                m_reviewBaselines.remove(reviewId);
                clearExport(reviewId);
                if (m_sessions.release(clientId, reviewId) > 0) uploads->releaseUpdates(shotId);
                synchronizeReviewSessions();
            }
        }
        finish(requestId, operation, true, {{QStringLiteral("reviewId"), reviewId}});
        return;
    }
    if (operation == QLatin1String("applyCarryForward")) {
        const QString reviewId = arguments.value(QStringLiteral("reviewId")).toString();
        const auto proposalIt = m_reviewProposals.constFind(reviewId);
        const auto baselineIt = m_reviewBaselines.constFind(reviewId);
        if (m_carryApplyBusy || arguments.size() != 2 || proposalIt == m_reviewProposals.cend()
            || baselineIt == m_reviewBaselines.cend()
            || arguments.value(QStringLiteral("proposalId")).toString() != proposalIt->requestId
            || !carryApplyCurrent(clientId, reviewId, proposalIt->requestId, baselineIt->shotId, proposalIt->expected,
                                  baselineIt->operationGeneration)) {
            fail(QStringLiteral("The next shot changed. Its settings were kept."));
            return;
        }
        const ReviewProposal proposal = proposalIt.value();
        const qint64 reviewedShot = baselineIt->shotId;
        const quint64 generation = baselineIt->operationGeneration;
        const QString path = m_controller->shotHistory()->databasePath();
        // Keep the token until completion so record-change events can cancel
        // an apply even after its database transaction has committed.
        m_carryApplyBusy = true;
        QPointer<NativeShotReviewActionSource> self(this);
        QThreadPool::globalInstance()->start(QRunnable::create(
            [self, requestId, clientId, reviewId, reviewedShot, generation, proposal, path] {
                QString error;
                const bool opened = withTempDb(path, "review_apply", [&](QSqlDatabase& db) {
                    error = commitShotReviewWrites(db, reviewedShot, proposal.persistentPlan,
                        proposal.patch, proposal.bag, proposal.recipe, [&] {
                            bool admitted = false;
                            if (self) QMetaObject::invokeMethod(self, [&] {
                                admitted = self && self->carryApplyCurrent(clientId, reviewId, proposal.requestId, reviewedShot,
                                                                           proposal.expected, generation);
                            }, Qt::BlockingQueuedConnection);
                            return admitted;
                        }, [&](const QVariantMap& bag, const QVariantMap& recipe) {
                            bool published = false;
                            if (self) QMetaObject::invokeMethod(self, [&] {
                                published = self && self->carryApplyCurrent(clientId, reviewId, proposal.requestId, reviewedShot,
                                                                           proposal.expected, generation)
                                    && self->m_controller->publishShotReviewPatch(proposal.patch, bag, recipe);
                            }, Qt::BlockingQueuedConnection);
                            return published;
                        });
                });
                if (!opened) error = QStringLiteral("The changes could not be saved.");
                if (!self) return;
                QMetaObject::invokeMethod(self, [self, requestId, reviewId, proposal, error] {
                    if (!self) return;
                    self->m_carryApplyBusy = false;
                    const auto currentProposal = self->m_reviewProposals.constFind(reviewId);
                    if (currentProposal != self->m_reviewProposals.cend()
                        && currentProposal->requestId == proposal.requestId)
                        self->m_reviewProposals.remove(reviewId);
                    if (error.isEmpty()) {
                        if (!proposal.persistentPlan.value(QStringLiteral("bagPatch")).toMap().isEmpty())
                            emit self->m_controller->bagStorage()->bagsChanged();
                        if (!proposal.persistentPlan.value(QStringLiteral("recipePatch")).toMap().isEmpty())
                            emit self->m_controller->recipeStorage()->recipesChanged();
                    }
                    self->finish(requestId, QStringLiteral("applyCarryForward"), error.isEmpty(),
                                 {{QStringLiteral("reviewId"), reviewId}}, error);
                }, Qt::QueuedConnection);
            }));
        return;
    }
    if (operation == QLatin1String("updateReviewShot")) {
        const QString reviewId = arguments.value(QStringLiteral("reviewId")).toString();
        bool shotOk = false;
        const qint64 requestedShot = arguments.value(QStringLiteral("shotId")).toLongLong(&shotOk);
        const QVariantMap metadata = arguments.value(QStringLiteral("metadata")).toMap();
        if (!ServiceValue::hasExactKeys(arguments, {QStringLiteral("reviewId"),
                QStringLiteral("shotId"), QStringLiteral("metadata")})
            || !shotOk || requestedShot <= 0 || !m_sessions.owns(clientId, reviewId, requestedShot)
            || !ShotHistoryStorage::validateShotMetadataRequest(metadata)
            || !m_controller->shotHistory() || !m_controller->shotHistory()->isReady()
            || !m_controller->shotUploads()) {
            fail(QStringLiteral("The shot update is unavailable."));
            return;
        }
        m_pendingReviewShotWrites.insert(requestId, requestedShot);
        m_controller->shotUploads()->expectHeldEdit(requestedShot);
        m_controller->shotHistory()->requestUpdateShotMetadataForRequest(requestId, requestedShot, metadata);
        return;
    }
    if (operation == QLatin1String("previewCarryForward")) {
        const QString reviewId = arguments.value(QStringLiteral("reviewId")).toString();
        bool shotOk = false;
        const qint64 requestedShot = arguments.value(QStringLiteral("shotId")).toLongLong(&shotOk);
        if (!ServiceValue::hasExactKeys(arguments, {QStringLiteral("reviewId"),
                QStringLiteral("shotId"), QStringLiteral("opening"), QStringLiteral("saved")})
            || !shotOk || requestedShot <= 0 || !m_sessions.owns(clientId, reviewId, requestedShot)) {
            fail(QStringLiteral("The shot review is no longer active."));
            return;
        }
        m_reviewProposals.remove(reviewId);
        if (!m_controller->shotHistory() || !m_controller->shotHistory()->isReady()) {
            fail(QStringLiteral("Shot history is unavailable."));
            return;
        }
        m_pendingCarryPreviews.insert(requestId, {clientId, reviewId, requestedShot,
            arguments.value(QStringLiteral("opening")).toMap(),
            arguments.value(QStringLiteral("saved")).toMap(), false, false});
        m_latestCarryPreview.insert(reviewId, requestId);
        m_controller->shotHistory()->requestShotForRequest(requestId, requestedShot);
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
