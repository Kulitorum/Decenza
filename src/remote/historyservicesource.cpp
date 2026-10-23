#include "historyservicesource.h"

#include "core/settings_network.h"
#include "history/shothistorystorage.h"
#include "servicepayload.h"
#include "servicevalue.h"

#include <QMetaType>
#include <QRegularExpression>
#include <QSet>
#include <cmath>

namespace {
// Leave framing/other service replies room under the 2 MiB QtRO transport cap.
constexpr qint64 kMaximumHistoryResponseBytes = 1536 * 1024;

bool validUuid(const QString& value)
{
    static const QRegularExpression pattern(QStringLiteral(
        "^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$"));
    return pattern.match(value).hasMatch();
}

bool validFavoriteGroup(const QString& group)
{
    return QStringList{QStringLiteral("bean"), QStringLiteral("profile"),
        QStringLiteral("bean_profile"), QStringLiteral("bean_profile_grinder"),
        QStringLiteral("bean_profile_grinder_weight")}.contains(group);
}

QStringList historySortFields()
{
    return {QStringLiteral("timestamp"), QStringLiteral("profile_name"),
            QStringLiteral("bean_brand"), QStringLiteral("bean_type"),
            QStringLiteral("enjoyment"), QStringLiteral("ratio"),
            QStringLiteral("duration_seconds"), QStringLiteral("dose_weight"),
            QStringLiteral("final_weight")};
}
}

NativeHistoryServiceSource::NativeHistoryServiceSource(ShotHistoryStorage* history,
                                                       SettingsNetwork* settings,
                                                       LoadShot loadShot,
                                                       QObject* parent)
    : HistoryStateSimpleSource(parent)
    , m_history(history)
    , m_settings(settings)
    , m_loadShot(std::move(loadShot))
{
    if (m_history) {
        connect(m_history, &ShotHistoryStorage::autoFavoritesRequestReady, this,
                [this](const QString& id, const QVariantList& rows, const QString& reason) {
            finish(id, QStringLiteral("autoFavorites"), reason.isEmpty(),
                   {{QStringLiteral("favorites"), rows}}, reason);
        });
        connect(m_history, &ShotHistoryStorage::autoFavoriteGroupDetailsRequestReady, this,
                [this](const QString& id, const QVariantMap& details, const QString& reason) {
            finish(id, QStringLiteral("autoFavoriteDetails"), reason.isEmpty(),
                   {{QStringLiteral("details"), details}}, reason);
        });
        connect(m_history, &ShotHistoryStorage::readyChanged,
                this, &NativeHistoryServiceSource::updateHistoryState);
        connect(m_history, &ShotHistoryStorage::totalShotsChanged,
                this, &NativeHistoryServiceSource::updateHistoryState);
        connect(m_history, &ShotHistoryStorage::historyDataChanged, this, [this] {
            ++m_historyRevision;
            setHistoryRevision(m_historyRevision);
            updateHistoryState();
            emit historyDataChanged();
        });
        connect(m_history, &ShotHistoryStorage::shotBadgesUpdated,
                this, &NativeHistoryServiceSource::shotBadgesUpdated);
        connect(m_history, &ShotHistoryStorage::shotPulledFromVisualizer,
                this, &NativeHistoryServiceSource::shotPulledFromVisualizer);
        connect(m_history, &ShotHistoryStorage::visualizerInfoUpdated,
                this, &NativeHistoryServiceSource::visualizerInfoUpdated);
        connect(m_history, &ShotHistoryStorage::decentUploadStateUpdated,
                this, &NativeHistoryServiceSource::decentUploadStateUpdated);
        connect(m_history, &ShotHistoryStorage::shotsFilteredRequestReady, this,
                [this](const QString& requestId, int offset, const QVariantList& results,
                       int totalCount, const QString& reason) {
            if (m_pending.value(requestId) != QLatin1String("page"))
                return;
            m_pending.remove(requestId);
            if (!ServicePayload::fits(results, kMaximumHistoryResponseBytes))
                emit pageReady(requestId, offset, {}, totalCount, QStringLiteral("tooLarge"));
            else
                emit pageReady(requestId, offset, results, totalCount, reason);
        });
        connect(m_history, &ShotHistoryStorage::mostRecentShotIdRequestReady, this,
                [this](const QString& requestId, qint64 shotId, const QString& reason) {
            if (m_pending.value(requestId) != QLatin1String("mostRecentShotId"))
                return;
            finish(requestId, QStringLiteral("mostRecentShotId"), reason.isEmpty(),
                   {{QStringLiteral("shotId"), shotId}}, reason);
        });
        connect(m_history, &ShotHistoryStorage::shotRequestReady, this,
                [this](const QString& requestId, qint64 shotId,
                       const QVariantMap& shot, const QString& reason) {
            finishShotMap(requestId, QStringLiteral("shot"), shotId, shot, reason);
        });
        connect(m_history, &ShotHistoryStorage::shotOutcomeRequestReady, this,
                [this](const QString& requestId, qint64 shotId,
                       const QVariantMap& outcome, const QString& reason) {
            finishShotMap(requestId, QStringLiteral("outcome"), shotId, outcome, reason);
        });
        connect(m_history, &ShotHistoryStorage::decentUploadStateRequestReady, this,
                [this](const QString& requestId, qint64 shotId,
                       const QVariantMap& state, const QString& reason) {
            finishShotMap(requestId, QStringLiteral("uploadState"), shotId, state, reason);
        });
        connect(m_history, &ShotHistoryStorage::distinctBaristasRequestReady, this,
                [this](const QString& requestId, const QStringList& values, const QString& reason) {
            if (m_pending.value(requestId) != QLatin1String("baristas"))
                return;
            finish(requestId, QStringLiteral("baristas"), reason.isEmpty(),
                   {{QStringLiteral("baristas"), values}}, reason);
        });
        connect(m_history, &ShotHistoryStorage::shotMetadataUpdateRequestFinished, this,
                [this](const QString& requestId, qint64 shotId, bool success, const QString& reason) {
            finish(requestId, QStringLiteral("metadata"), success,
                   {{QStringLiteral("shotId"), shotId}}, reason);
        });
        connect(m_history, &ShotHistoryStorage::shotDeleteRequestFinished, this,
                [this](const QString& requestId, qint64 shotId, bool success, const QString& reason) {
            finish(requestId, QStringLiteral("deleteShot"), success,
                   {{QStringLiteral("shotId"), shotId}}, reason);
        });
        connect(m_history, &ShotHistoryStorage::shotsDeleteRequestFinished, this,
                [this](const QString& requestId, bool success, const QVariantList& shotIds,
                       const QString& reason) {
            finish(requestId, QStringLiteral("deleteShots"), success,
                   {{QStringLiteral("shotIds"), shotIds}}, reason);
        });
    }
    if (m_settings) {
        const auto changed = [this] { refresh(); };
        connect(m_settings, &SettingsNetwork::autoFavoritesGroupByChanged, this, changed);
        connect(m_settings, &SettingsNetwork::autoFavoritesMaxItemsChanged, this, changed);
        connect(m_settings, &SettingsNetwork::autoFavoritesHideUnratedChanged, this, changed);
        connect(m_settings, &SettingsNetwork::autoFavoritesOpenBrewSettingsChanged, this, changed);
        connect(m_settings, &SettingsNetwork::autoFavoriteGraphHeightChanged, this, changed);
        connect(m_settings, &SettingsNetwork::savedSearchesChanged, this, changed);
        connect(m_settings, &SettingsNetwork::shotHistorySortFieldChanged, this, changed);
        connect(m_settings, &SettingsNetwork::shotHistorySortDirectionChanged, this, changed);
    }
    refresh();
}

void NativeHistoryServiceSource::refresh()
{
    updateHistoryState();
    const QVariantMap next = capturePreferences();
    if (preferences() != next) {
        setPreferences(next);
        setPreferencesRevision(++m_preferencesRevision);
    }
}

void NativeHistoryServiceSource::updateHistoryState()
{
    setReady(m_history && m_history->isReady());
    if (m_history) {
        setTotalShots(m_history->totalShots());
        setLastSavedShotId(m_history->lastSavedShotId());
    } else {
        setTotalShots(0);
        setLastSavedShotId(0);
    }
}

QVariantMap NativeHistoryServiceSource::capturePreferences() const
{
    if (!m_settings)
        return {};
    return {{QStringLiteral("autoFavoriteGraphHeight"), m_settings->autoFavoriteGraphHeight()},
            {QStringLiteral("autoFavoritesGroupBy"), m_settings->autoFavoritesGroupBy()},
            {QStringLiteral("autoFavoritesMaxItems"), m_settings->autoFavoritesMaxItems()},
            {QStringLiteral("autoFavoritesHideUnrated"), m_settings->autoFavoritesHideUnrated()},
            {QStringLiteral("autoFavoritesOpenBrewSettings"), m_settings->autoFavoritesOpenBrewSettings()},
            {QStringLiteral("sortField"), m_settings->shotHistorySortField()},
            {QStringLiteral("sortDirection"), m_settings->shotHistorySortDirection()},
            {QStringLiteral("savedSearches"), m_settings->savedSearches()}};
}

bool NativeHistoryServiceSource::validRequest(const QString& requestId,
                                              const QString& operation)
{
    if (!validUuid(requestId) || m_pending.contains(requestId))
        return false;
    if (m_pending.size() >= 128) {
        // Duplicate IDs cannot receive a second response: that would falsely
        // complete the original operation. A fresh valid caller at capacity
        // does receive a terminal result instead of waiting forever.
        if (operation == QLatin1String("page"))
            emit pageReady(requestId, 0, {}, 0, QStringLiteral("busy"));
        else
            emit operationCompleted(requestId, operation, false, {}, QStringLiteral("busy"));
        return false;
    }
    m_pending.insert(requestId, operation);
    return true;
}

void NativeHistoryServiceSource::finish(const QString& requestId, const QString& operation,
                                        bool success, const QVariantMap& result,
                                        const QString& reason)
{
    if (m_pending.value(requestId) != operation)
        return;
    m_pending.remove(requestId);
    m_pendingShotLoads.remove(requestId);
    if (!ServicePayload::fits(result, kMaximumHistoryResponseBytes))
        emit operationCompleted(requestId, operation, false, {}, QStringLiteral("tooLarge"));
    else
        emit operationCompleted(requestId, operation, success, result, reason);
}

void NativeHistoryServiceSource::finishShotMap(const QString& requestId,
                                               const QString& operation, qint64 shotId,
                                               const QVariantMap& result,
                                               const QString& reason)
{
    if (m_pending.value(requestId) != operation)
        return;
    QVariantMap payload;
    if (operation == QLatin1String("shot")) {
        payload.insert(QStringLiteral("shot"), result);
    } else {
        payload = result;
    }
    payload.insert(QStringLiteral("shotId"), shotId);
    finish(requestId, operation, reason.isEmpty(), payload, reason);
}

void NativeHistoryServiceSource::requestPage(QString requestId, QVariantMap filter,
                                             int offset, int limit)
{
    if (!validRequest(requestId, QStringLiteral("page")))
        return;
    if (!m_history || !m_history->isReady() || filter.size() > 64
        || offset < 0 || limit < 1 || limit > 5000) {
        m_pending.remove(requestId);
        emit pageReady(requestId, offset, {}, 0,
                       !m_history || !m_history->isReady()
                           ? QStringLiteral("unavailable") : QStringLiteral("invalidRequest"));
        return;
    }
    m_history->requestShotsFilteredForRequest(requestId, filter, offset, limit);
}

void NativeHistoryServiceSource::requestAutoFavorites(QString requestId, QString groupBy, int maxItems)
{
    const QString op = QStringLiteral("autoFavorites");
    if (!validRequest(requestId, op)) return;
    if (!ready() || !validFavoriteGroup(groupBy) || maxItems < 5 || maxItems > 50) {
        finish(requestId, op, false, {}, QStringLiteral("invalidRequest"));
        return;
    }
    m_history->requestAutoFavorites(groupBy, maxItems, requestId);
}

void NativeHistoryServiceSource::requestAutoFavoriteGroupDetails(QString requestId, QVariantMap group)
{
    const QString op = QStringLiteral("autoFavoriteDetails");
    if (!validRequest(requestId, op)) return;
    const QSet<QString> keys{QStringLiteral("groupBy"), QStringLiteral("beanBrand"),
        QStringLiteral("beanType"), QStringLiteral("profileName"), QStringLiteral("equipmentId"),
        QStringLiteral("grinderSetting"), QStringLiteral("doseBucket"), QStringLiteral("targetWeight")};
    bool valid = ready() && validFavoriteGroup(group.value(QStringLiteral("groupBy")).toString());
    for (auto it = group.cbegin(); it != group.cend(); ++it)
        valid = valid && keys.contains(it.key()) && it.value().toString().size() <= 1024;
    bool idOk = false, doseOk = false, weightOk = false;
    const qint64 equipmentId = group.value(QStringLiteral("equipmentId"), 0).toLongLong(&idOk);
    const double dose = group.value(QStringLiteral("doseBucket"), 0).toDouble(&doseOk);
    const double weight = group.value(QStringLiteral("targetWeight"), 0).toDouble(&weightOk);
    if (!valid || !idOk || equipmentId < 0 || !doseOk || !weightOk
        || !std::isfinite(dose) || !std::isfinite(weight) || dose < 0 || weight < 0) {
        finish(requestId, op, false, {}, QStringLiteral("invalidRequest"));
        return;
    }
    m_history->requestAutoFavoriteGroupDetails(group.value(QStringLiteral("groupBy")).toString(),
        group.value(QStringLiteral("beanBrand")).toString(), group.value(QStringLiteral("beanType")).toString(),
        group.value(QStringLiteral("profileName")).toString(), equipmentId,
        group.value(QStringLiteral("grinderSetting")).toString(), dose, weight, requestId);
}

void NativeHistoryServiceSource::requestMostRecentShotId(QString requestId)
{
    if (!validRequest(requestId, QStringLiteral("mostRecentShotId")))
        return;
    if (!m_history || !m_history->isReady()) {
        finish(requestId, QStringLiteral("mostRecentShotId"), false,
               {{QStringLiteral("shotId"), qint64(-1)}}, QStringLiteral("unavailable"));
        return;
    }
    m_history->requestMostRecentShotIdForRequest(requestId);
}

void NativeHistoryServiceSource::requestShot(QString requestId, qint64 shotId)
{
    if (!validRequest(requestId, QStringLiteral("shot")))
        return;
    if (!m_history || !m_history->isReady() || shotId <= 0) {
        finish(requestId, QStringLiteral("shot"), false,
               {{QStringLiteral("shotId"), shotId}}, QStringLiteral("unavailable"));
        return;
    }
    m_history->requestShotForRequest(requestId, shotId);
}

void NativeHistoryServiceSource::requestOutcome(QString requestId, qint64 shotId)
{
    if (!validRequest(requestId, QStringLiteral("outcome")))
        return;
    if (!m_history || !m_history->isReady() || shotId <= 0) {
        finish(requestId, QStringLiteral("outcome"), false,
               {{QStringLiteral("shotId"), shotId}}, QStringLiteral("unavailable"));
        return;
    }
    m_history->requestShotOutcomeForRequest(requestId, shotId);
}

void NativeHistoryServiceSource::requestUploadState(QString requestId, qint64 shotId)
{
    if (!validRequest(requestId, QStringLiteral("uploadState")))
        return;
    if (!m_history || !m_history->isReady() || shotId <= 0) {
        finish(requestId, QStringLiteral("uploadState"), false,
               {{QStringLiteral("shotId"), shotId}}, QStringLiteral("unavailable"));
        return;
    }
    m_history->requestDecentUploadStateForRequest(requestId, shotId);
}

void NativeHistoryServiceSource::requestBaristas(QString requestId)
{
    if (!validRequest(requestId, QStringLiteral("baristas")))
        return;
    if (!m_history || !m_history->isReady()) {
        finish(requestId, QStringLiteral("baristas"), false, {}, QStringLiteral("unavailable"));
        return;
    }
    m_history->requestDistinctBaristasForRequest(requestId);
}

void NativeHistoryServiceSource::updateMetadata(QString requestId, qint64 shotId,
                                               QVariantMap metadata)
{
    if (!validRequest(requestId, QStringLiteral("metadata")))
        return;
    QString refusal;
    if (!m_history || !m_history->isReady() || shotId <= 0
        || !ShotHistoryStorage::validateShotMetadataRequest(metadata, &refusal)) {
        finish(requestId, QStringLiteral("metadata"), false,
               {{QStringLiteral("shotId"), shotId}},
               !refusal.isEmpty() ? refusal : QStringLiteral("invalidRequest"));
        return;
    }
    m_history->requestUpdateShotMetadataForRequest(requestId, shotId, metadata);
}

void NativeHistoryServiceSource::deleteShot(QString requestId, qint64 shotId)
{
    if (!validRequest(requestId, QStringLiteral("deleteShot")))
        return;
    if (!m_history || !m_history->isReady() || shotId <= 0) {
        finish(requestId, QStringLiteral("deleteShot"), false,
               {{QStringLiteral("shotId"), shotId}}, QStringLiteral("unavailable"));
        return;
    }
    m_history->requestDeleteShotForRequest(requestId, shotId);
}

void NativeHistoryServiceSource::deleteShots(QString requestId, QVariantList shotIds)
{
    if (!validRequest(requestId, QStringLiteral("deleteShots")))
        return;
    QSet<qint64> unique;
    if (!m_history || !m_history->isReady() || shotIds.isEmpty() || shotIds.size() > 500) {
        finish(requestId, QStringLiteral("deleteShots"), false, {}, QStringLiteral("invalidRequest"));
        return;
    }
    for (const QVariant& value : shotIds) {
        bool ok = false;
        const qint64 id = value.toLongLong(&ok);
        if (!ok || id <= 0 || unique.contains(id)) {
            finish(requestId, QStringLiteral("deleteShots"), false, {}, QStringLiteral("invalidRequest"));
            return;
        }
        unique.insert(id);
    }
    m_history->deleteShotsForRequest(requestId, std::move(shotIds));
}

void NativeHistoryServiceSource::loadShotWithMetadata(QString requestId, qint64 shotId)
{
    if (!validRequest(requestId, QStringLiteral("loadShotWithMetadata")))
        return;
    if (!m_loadShot || !m_history || !m_history->isReady() || shotId <= 0) {
        finish(requestId, QStringLiteral("loadShotWithMetadata"), false, {},
               QStringLiteral("unavailable"));
        return;
    }
    m_pendingShotLoads.insert(requestId, shotId);
    m_loadShot(requestId, shotId);
}

void NativeHistoryServiceSource::completeShotLoad(const QString& requestId, qint64 shotId,
                                                  bool success)
{
    if (m_pending.value(requestId) != QLatin1String("loadShotWithMetadata")
        || m_pendingShotLoads.value(requestId, -1) != shotId)
        return;
    finish(requestId, QStringLiteral("loadShotWithMetadata"), success,
           {{QStringLiteral("shotId"), shotId}},
           success ? QString() : QStringLiteral("could not load shot"));
}

void NativeHistoryServiceSource::setPreference(QString requestId, QString name,
                                               QVariant value, quint64 expectedRevision)
{
    if (!validRequest(requestId, QStringLiteral("preference")))
        return;
    if (!m_settings) {
        finish(requestId, QStringLiteral("preference"), false, {}, QStringLiteral("unavailable"));
        return;
    }
    refresh();
    if (expectedRevision != preferencesRevision()) {
        finish(requestId, QStringLiteral("preference"), false,
               {{QStringLiteral("preferences"), preferences()},
                {QStringLiteral("revision"), preferencesRevision()}},
               QStringLiteral("stalePreferences"));
        return;
    }

    bool accepted = false;
    if (name == QLatin1String("autoFavoriteGraphHeight")) {
        double height = 0;
        accepted = ServiceValue::finiteNumber(value, &height) && height >= 50 && height <= 1600;
        if (accepted) m_settings->setAutoFavoriteGraphHeight(height);
    } else if (name == QLatin1String("autoFavoritesGroupBy")) {
        accepted = value.metaType().id() == QMetaType::QString && validFavoriteGroup(value.toString());
        if (accepted) m_settings->setAutoFavoritesGroupBy(value.toString());
    } else if (name == QLatin1String("autoFavoritesMaxItems")) {
        int n = 0;
        accepted = ServiceValue::boundedInteger(value, 5, 50, &n);
        if (accepted) m_settings->setAutoFavoritesMaxItems(n);
    } else if (name == QLatin1String("autoFavoritesHideUnrated")
               || name == QLatin1String("autoFavoritesOpenBrewSettings")) {
        accepted = value.metaType().id() == QMetaType::Bool;
        if (accepted && name == QLatin1String("autoFavoritesHideUnrated"))
            m_settings->setAutoFavoritesHideUnrated(value.toBool());
        else if (accepted) m_settings->setAutoFavoritesOpenBrewSettings(value.toBool());
    } else if (name == QLatin1String("sort")) {
        const QVariantMap sort = value.toMap();
        const QString field = sort.value(QStringLiteral("field")).toString();
        const QString direction = sort.value(QStringLiteral("direction")).toString();
        if (sort.size() == 2 && sort.contains(QStringLiteral("field"))
            && sort.contains(QStringLiteral("direction"))
            && historySortFields().contains(field)
            && (direction == QLatin1String("ASC") || direction == QLatin1String("DESC"))) {
            m_settings->setShotHistorySortField(field);
            m_settings->setShotHistorySortDirection(direction);
            accepted = true;
        }
    } else if (name == QLatin1String("savedSearches")) {
        QStringList searches;
        bool allStrings = true;
        if (value.metaType().id() == QMetaType::QStringList) {
            searches = value.toStringList();
        } else if (value.metaType().id() == QMetaType::QVariantList) {
            const QVariantList entries = value.toList();
            for (const QVariant& entry : entries) {
                if (entry.metaType().id() != QMetaType::QString) {
                    allStrings = false;
                    break;
                }
                searches.append(entry.toString());
            }
        }
        if (value.metaType().id() == QMetaType::QStringList
            || value.metaType().id() == QMetaType::QVariantList) {
            QSet<QString> unique;
            accepted = allStrings && searches.size() <= 30;
            for (const QString& search : searches) {
                const QString normalized = search.trimmed();
                if (search.isEmpty() || normalized != search || search.size() > 256
                    || unique.contains(search)) {
                    accepted = false;
                    break;
                }
                unique.insert(search);
            }
            if (accepted)
                m_settings->setSavedSearches(searches);
        }
    }

    refresh();
    if (!accepted) {
        finish(requestId, QStringLiteral("preference"), false,
               {{QStringLiteral("preferences"), preferences()},
                {QStringLiteral("revision"), preferencesRevision()}},
               QStringLiteral("invalidPreference"));
        return;
    }
    finish(requestId, QStringLiteral("preference"), true,
           {{QStringLiteral("preferences"), preferences()},
            {QStringLiteral("revision"), preferencesRevision()}}, {});
}
