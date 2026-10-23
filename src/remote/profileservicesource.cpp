#include "profileservicesource.h"

#include "../controllers/profilemanager.h"
#include "../core/settings_app.h"
#include "../core/settings_calibration.h"
#include "../profile/recipeprofiledisplay.h"
#include "../ai/shotsummarizer.h"
#include "servicepayload.h"

#include <QSet>
#include <QMetaType>
#include <cmath>
#include <utility>

namespace {
constexpr qint64 kMaximumProfilePayloadBytes = 1024 * 1024;

bool variantMapWithinLimit(const QVariantMap& value)
{
    return ServicePayload::fits(value, kMaximumProfilePayloadBytes);
}
}

NativeProfileServiceSource::NativeProfileServiceSource(ProfileManager* profiles,
                                                       SettingsApp* app,
                                                       SettingsCalibration* calibration,
                                                       QObject* parent)
    : ProfileStateSimpleSource(parent), m_profiles(profiles), m_app(app),
      m_calibration(calibration)
{
    if (m_profiles) {
        connect(m_profiles, &ProfileManager::profilesChanged, this, [this] { publish(true, true); });
        connect(m_profiles, &ProfileManager::profileUsageChanged, this, [this] { publish(); });
        connect(m_profiles, &ProfileManager::currentProfileChanged, this, [this] { publish(); });
        connect(m_profiles, &ProfileManager::profileModifiedChanged, this, [this] { publish(); });
    }
    if (m_app) {
        connect(m_app, &SettingsApp::favoriteProfilesChanged, this, [this] { publish(true); });
        connect(m_app, &SettingsApp::favoriteProfileOrderChanged, this, [this] { publish(true); });
        connect(m_app, &SettingsApp::autoLoadProfileFilenameChanged, this, [this] { publish(); });
        connect(m_app, &SettingsApp::autoLoadRevertMinutesChanged, this, [this] { publish(); });
    }
    if (m_calibration) {
        connect(m_calibration, &SettingsCalibration::flowCalibrationMultiplierChanged,
                this, [this] { publish(); });
        connect(m_calibration, &SettingsCalibration::autoFlowCalibrationChanged,
                this, [this] { publish(); });
        connect(m_calibration, &SettingsCalibration::perProfileFlowCalibrationChanged,
                this, [this] { publish(); });
    }
    publish(true, true);
}

void NativeProfileServiceSource::refresh()
{
    publish(true, true);
}

QVariantMap NativeProfileServiceSource::captureProfileSummaries() const
{
    QVariantMap summaries;
    if (!m_profiles) return summaries;
    const auto numbers = m_profiles->recipeProfileDisplaySnapshot();
    QVariantMap metadataByTitle;
    for (const QVariant& item : m_profiles->allProfilesList()) {
        const QVariantMap row = item.toMap();
        if (!metadataByTitle.contains(row.value(QStringLiteral("title")).toString()))
            metadataByTitle.insert(row.value(QStringLiteral("title")).toString(), row);
    }
    for (auto it = numbers.cbegin(); it != numbers.cend(); ++it) {
        const RecipeProfileDisplayNumbers& n = it.value();
        QVariantList temps;
        temps.reserve(n.stepTempsC.size());
        for (double value : n.stepTempsC) temps.append(value);
        QVariantMap summary{
            {QStringLiteral("valid"), n.valid},
            {QStringLiteral("baseTempC"), n.baseTempC},
            {QStringLiteral("yieldG"), n.yieldG},
            {QStringLiteral("stepTempsC"), temps}
        };
        const QVariantMap metadata = metadataByTitle.value(it.key()).toMap();
        for (const QString& key : {QStringLiteral("beverageType"), QStringLiteral("hasRecommendedDose"),
                                   QStringLiteral("recommendedDose"), QStringLiteral("stepCount"),
                                   QStringLiteral("maximumPressure"), QStringLiteral("maximumFlow")}) {
            if (metadata.contains(key)) summary.insert(key, metadata.value(key));
        }
        summaries.insert(it.key(), summary);
    }
    return summaries;
}

QVariantMap NativeProfileServiceSource::captureFlowCalibration() const
{
    QVariantMap profiles;
    if (!m_calibration || !m_profiles) return profiles;
    for (const QVariant& value : m_profiles->availableProfiles()) {
        const QString filename = value.toMap().value(QStringLiteral("name")).toString();
        if (filename.isEmpty() || profiles.contains(filename)) continue;
        profiles.insert(filename, QVariantMap{
            {QStringLiteral("effectiveFlowCalibration"),
             m_calibration->effectiveFlowCalibration(filename)},
            {QStringLiteral("hasProfileFlowCalibration"),
             m_calibration->hasProfileFlowCalibration(filename)}
        });
    }
    return {
        {QStringLiteral("perProfileFlowCalVersion"), m_calibration->perProfileFlowCalVersion()},
        {QStringLiteral("autoFlowCalibration"), m_calibration->autoFlowCalibration()},
        {QStringLiteral("flowCalibrationMultiplier"), m_calibration->flowCalibrationMultiplier()},
        {QStringLiteral("profiles"), profiles}
    };
}

QVariantMap NativeProfileServiceSource::captureRoastAffinities() const
{
    QVariantMap result;
    if (!m_profiles) return result;
    QSet<QString> seen;
    for (const QVariant& value : m_profiles->allProfilesList()) {
        const QString title = value.toMap().value(QStringLiteral("title")).toString();
        if (seen.contains(title)) continue;
        seen.insert(title);
        result.insert(title, ShotSummarizer::roastAffinityForTitle(title));
    }
    return result;
}

QVariantMap NativeProfileServiceSource::captureGrindDirections() const
{
    QVariantMap result;
    if (!m_profiles) return result;
    QStringList titles;
    QSet<QString> seen;
    for (const QVariant& value : m_profiles->allProfilesList()) {
        const QString title = value.toMap().value(QStringLiteral("title")).toString();
        if (!seen.contains(title)) { seen.insert(title); titles.append(title); }
    }
    for (const QString& sourceTitle : titles) {
        QVariantMap targets;
        for (const QString& targetTitle : titles) {
            const QString direction = m_profiles->grindDirectionBetween(sourceTitle, targetTitle);
            if (!direction.isEmpty()) targets.insert(targetTitle, direction);
        }
        if (!targets.isEmpty()) result.insert(sourceTitle, targets);
    }
    return result;
}

void NativeProfileServiceSource::publish(bool revisionChanged, bool rebuildCatalog)
{
    if (revisionChanged) ++m_catalogRevision;
    if (rebuildCatalog && m_profiles) {
        m_allProfiles = m_profiles->allProfilesList();
        m_availableProfiles = m_profiles->availableProfiles();
        m_installedTitles = m_profiles->installedProfileTitles();
        m_summaries = captureProfileSummaries();
        m_roastAffinities = captureRoastAffinities();
        m_grindDirections = captureGrindDirections();
    }
    QVariantList favorites = m_app ? m_app->favoriteProfiles() : QVariantList();
    QString favoriteOrder = m_app ? m_app->favoriteProfileOrder() : QStringLiteral("usage");
    QVariantMap usage = m_profiles ? m_profiles->profileUsage() : QVariantMap();
    QString baseName = m_profiles ? m_profiles->baseProfileName() : QString();
    QString previousName = m_profiles ? m_profiles->previousProfileName() : QString();
    const bool modified = m_profiles && m_profiles->isProfileModified();
    const bool readOnly = m_profiles && m_profiles->isCurrentProfileReadOnly();
    const QString autoLoad = m_app ? m_app->autoLoadProfileFilename() : QString();
    const int autoLoadRevert = m_app ? m_app->autoLoadRevertMinutes() : 0;
    const QVariantMap flowCalibration = captureFlowCalibration();
    setCatalogRevision(m_catalogRevision);
    QVariantMap current;
    if (m_profiles) {
        QVariantList temperatures;
        for (const auto& frame : m_profiles->currentProfile().steps())
            temperatures.append(frame.temperature);
        current = {{QStringLiteral("name"), m_profiles->currentProfileName()},
                   {QStringLiteral("title"), m_profiles->currentProfileTitle()},
                   {QStringLiteral("editorType"), m_profiles->currentEditorType()},
                   {QStringLiteral("filename"), m_profiles->baseProfileName()},
                   {QStringLiteral("beverageType"), m_profiles->currentProfileBeverageType()},
                   {QStringLiteral("beverageGroup"), m_profiles->currentProfileBeverageGroup()},
                   {QStringLiteral("hasRecommendedDose"), m_profiles->profileHasRecommendedDose()},
                   {QStringLiteral("recommendedDose"), m_profiles->profileRecommendedDose()},
                   {QStringLiteral("isMaintenance"), m_profiles->currentProfileIsMaintenance()},
                   {QStringLiteral("stepTempsC"), temperatures},
                   {QStringLiteral("espressoTemperatureC"), m_profiles->profileTargetTemperature()},
                   {QStringLiteral("targetWeightG"), m_profiles->profileTargetWeight()}};
    }
    QVariantMap snapshot{
        {QStringLiteral("ready"), m_profiles != nullptr},
        {QStringLiteral("loading"), false},
        {QStringLiteral("errorMessage"), QStringLiteral("Profile catalog exceeds the 1 MiB transport limit")},
        {QStringLiteral("catalogRevision"), m_catalogRevision},
        {QStringLiteral("allProfilesList"), m_allProfiles},
        {QStringLiteral("availableProfiles"), m_availableProfiles},
        {QStringLiteral("installedProfileTitles"), QVariant::fromValue(m_installedTitles)},
        {QStringLiteral("baseProfileName"), baseName},
        {QStringLiteral("previousProfileName"), previousName},
        {QStringLiteral("profileModified"), modified},
        {QStringLiteral("currentProfileReadOnly"), readOnly},
        {QStringLiteral("profileUsage"), usage},
        {QStringLiteral("favoriteProfiles"), favorites},
        {QStringLiteral("favoriteProfileOrder"), favoriteOrder},
        {QStringLiteral("autoLoadProfileFilename"), autoLoad},
        {QStringLiteral("autoLoadRevertMinutes"), autoLoadRevert},
        {QStringLiteral("flowCalibration"), flowCalibration},
        {QStringLiteral("profileSummaries"), m_summaries},
        {QStringLiteral("currentProfileSummary"), current},
        {QStringLiteral("roastAffinities"), m_roastAffinities},
        {QStringLiteral("grindDirections"), m_grindDirections}
    };
    const bool oversized = !variantMapWithinLimit(snapshot);
    setReady(m_profiles != nullptr && !oversized);
    setLoading(false);
    setErrorMessage(!m_profiles ? QStringLiteral("Profile service is unavailable")
                                : oversized ? QStringLiteral("Profile catalog exceeds the 1 MiB transport limit")
                                            : QString());
    // Publish either the complete catalogue or a bounded empty error state.
    // Never truncate profile data or expose a partially updated snapshot.
    setAllProfilesList(oversized ? QVariantList{} : m_allProfiles);
    setAvailableProfiles(oversized ? QVariantList{} : m_availableProfiles);
    setInstalledProfileTitles(oversized ? QStringList{} : m_installedTitles);
    setBaseProfileName(oversized ? QString() : baseName);
    setPreviousProfileName(oversized ? QString() : previousName);
    setProfileModified(!oversized && modified);
    setCurrentProfileReadOnly(!oversized && readOnly);
    setProfileUsage(oversized ? QVariantMap{} : usage);
    setFavoriteProfiles(oversized ? QVariantList{} : favorites);
    setFavoriteProfileOrder(oversized ? QStringLiteral("usage") : favoriteOrder);
    setAutoLoadProfileFilename(oversized ? QString() : autoLoad);
    setAutoLoadRevertMinutes(oversized ? 0 : autoLoadRevert);
    setFlowCalibration(oversized ? QVariantMap{} : flowCalibration);
    setCurrentProfileSummary(oversized ? QVariantMap{} : current);
    setProfileSummaries(oversized ? QVariantMap{} : m_summaries);
    setRoastAffinities(oversized ? QVariantMap{} : m_roastAffinities);
    setGrindDirections(oversized ? QVariantMap{} : m_grindDirections);
}

bool NativeProfileServiceSource::knownFilename(const QString& filename) const
{
    if (!m_profiles || filename.isEmpty() || filename.size() > 256
        || filename.contains(QLatin1Char('/')) || filename.contains(QLatin1Char('\\'))
        || filename == QLatin1String(".") || filename == QLatin1String(".."))
        return false;
    for (const QVariant& row : m_profiles->availableProfiles())
        if (row.toMap().value(QStringLiteral("name")).toString() == filename)
            return true;
    return false;
}

void NativeProfileServiceSource::finishRead(const QString& requestId)
{
    m_reads.finish(requestId);
}

void NativeProfileServiceSource::requestProfileDetail(QString requestId, QString filename)
{
    const auto start = m_reads.begin(requestId);
    if (start != ServiceRequestIds::Start::Accepted) return;
    if (!m_profiles || (!filename.isEmpty() && !knownFilename(filename))) {
        finishRead(requestId);
        emit profileDetailResult(requestId, filename, {}, m_profiles
            ? QStringLiteral("profileNotFound") : QStringLiteral("disconnected"));
        return;
    }
    const QVariantMap detail = m_profiles->editorProfileDetail(filename);
    finishRead(requestId);
    if (detail.isEmpty()) {
        emit profileDetailResult(requestId, filename, {}, QStringLiteral("profileNotFound"));
    } else if (!variantMapWithinLimit({{QStringLiteral("requestId"), requestId},
                                       {QStringLiteral("filename"), filename},
                                       {QStringLiteral("detail"), detail},
                                       {QStringLiteral("reason"), QStringLiteral("responseTooLarge")}})) {
        emit profileDetailResult(requestId, filename, {}, QStringLiteral("responseTooLarge"));
    } else {
        emit profileDetailResult(requestId, filename, detail, QString());
    }
}

void NativeProfileServiceSource::requestKnowledge(QString requestId, QString title,
                                                  QString profileJson)
{
    const auto start = m_reads.begin(requestId);
    if (start != ServiceRequestIds::Start::Accepted) return;
    constexpr qsizetype kMaximumKnowledgeProfileChars = 256 * 1024;
    if (!m_profiles || title.size() > 512 || profileJson.size() > kMaximumKnowledgeProfileChars) {
        finishRead(requestId);
        QVariantMap errorResponse{{QStringLiteral("requestId"), requestId},
                                  {QStringLiteral("title"), title},
                                  {QStringLiteral("profileJson"), profileJson},
                                  {QStringLiteral("payload"), QVariantMap{}},
                                  {QStringLiteral("reason"), QStringLiteral("invalidRequest")}};
        QString responseTitle = title;
        QString responseJson = profileJson;
        if (!variantMapWithinLimit(errorResponse)) {
            responseTitle.clear();
            responseJson.clear();
        }
        emit knowledgeResult(requestId, responseTitle, responseJson, {}, m_profiles
            ? QStringLiteral("invalidRequest") : QStringLiteral("disconnected"));
        return;
    }
    QVariantMap payload;
    const QString content = m_profiles->profileKnowledgeContent(title);
    payload.insert(QStringLiteral("hasKnowledge"), !content.isEmpty());
    payload.insert(QStringLiteral("content"), content);
    payload.insert(QStringLiteral("candidateNames"), m_profiles->profileKbCandidateNames(title));
    payload.insert(QStringLiteral("derivedFrom"), m_profiles->profileKbDerivedFrom(title));
    payload.insert(QStringLiteral("dialInDiff"), m_profiles->profileDialInDiff(title));
    payload.insert(QStringLiteral("dialInDiffForJson"), profileJson.isEmpty()
        ? QVariantMap{} : m_profiles->profileDialInDiffForJson(profileJson));
    finishRead(requestId);
    const QVariantMap response{{QStringLiteral("requestId"), requestId},
                               {QStringLiteral("title"), title},
                               {QStringLiteral("profileJson"), profileJson},
                               {QStringLiteral("payload"), payload},
                               {QStringLiteral("reason"), QStringLiteral("responseTooLarge")}};
    if (!variantMapWithinLimit(response)) {
        const QVariantMap boundedError{{QStringLiteral("requestId"), requestId},
                                       {QStringLiteral("title"), title},
                                       {QStringLiteral("profileJson"), profileJson},
                                       {QStringLiteral("payload"), QVariantMap{}},
                                       {QStringLiteral("reason"), QStringLiteral("responseTooLarge")}};
        if (variantMapWithinLimit(boundedError))
            emit knowledgeResult(requestId, title, profileJson, {}, QStringLiteral("responseTooLarge"));
        else
            emit knowledgeResult(requestId, title, {}, {}, QStringLiteral("responseTooLarge"));
        return;
    }
    emit knowledgeResult(requestId, title, profileJson, payload, QString());
}

void NativeProfileServiceSource::finishOperation(const QString& requestId,
                                                 const QString& operation,
                                                 const QString& filename,
                                                 bool success, const QString& reason,
                                                 const QVariantMap& result)
{
    if (!m_operations.finish(requestId)) return;
    emit operationCompleted(requestId, operation, filename, success, reason, result);
}

void NativeProfileServiceSource::execute(QString requestId, QString operation,
                                         QString filename, QVariantMap fields,
                                         qint64 expectedRevision)
{
    const auto requestStart = m_operations.begin(requestId);
    if (requestStart != ServiceRequestIds::Start::Accepted) return;
    if (!m_profiles || !m_app) {
        finishOperation(requestId, operation, filename, false,
                        QStringLiteral("disconnected"));
        return;
    }
    if (operation == QLatin1String("saveDraft") || operation == QLatin1String("tryDraft")) {
        const bool tryOnly = operation == QLatin1String("tryDraft");
        const QVariant draft = fields.value(QStringLiteral("draft"));
        const QVariant revision = fields.value(QStringLiteral("sourceRevision"));
        const QVariant title = fields.value(QStringLiteral("saveAsTitle"));
        const QVariant overwrite = fields.value(QStringLiteral("overwriteRevision"));
        if (fields.size() != (tryOnly ? 2 : 4) || draft.metaType().id() != QMetaType::QVariantMap
            || revision.metaType().id() != QMetaType::QString || revision.toString().size() > 64
            || (!tryOnly && title.metaType().id() != QMetaType::QString)
            || (!tryOnly && (overwrite.metaType().id() != QMetaType::QString || overwrite.toString().size() > 64))
            || (!filename.isEmpty() && !knownFilename(filename)) || !variantMapWithinLimit(fields)) {
            finishOperation(requestId, operation, filename, false, QStringLiteral("invalidRequest"));
            return;
        }
        QString reason;
        const QVariantMap result = m_profiles->commitEditorDraft(filename, draft.toMap(),
            revision.toString(), tryOnly ? QString() : title.toString(), tryOnly, &reason, overwrite.toString());
        const bool success = reason.isEmpty() && !result.isEmpty();
        if (success) publish(!tryOnly, !tryOnly);
        finishOperation(requestId, operation, filename, success, reason, result);
        return;
    }
    if (expectedRevision != m_catalogRevision) {
        finishOperation(requestId, operation, filename, false, QStringLiteral("conflict"));
        return;
    }

    bool success = false;
    QString reason;
    QVariantMap result;
    if (operation == QLatin1String("setFavoritesOrder")) {
        const QVariant modeValue = fields.value(QStringLiteral("mode"));
        const QString mode = modeValue.toString();
        const QVariant filenamesValue = fields.value(QStringLiteral("filenames"));
        const QVariantList requested = filenamesValue.toList();
        if (fields.size() != 2 || modeValue.metaType().id() != QMetaType::QString
            || filenamesValue.metaType().id() != QMetaType::QVariantList
            || (mode != QLatin1String("custom")
                && mode != QLatin1String("alpha") && mode != QLatin1String("usage"))) {
            reason = QStringLiteral("invalidRequest");
        } else {
            QSet<QString> current;
            for (const QVariant& value : m_app->favoriteProfiles())
                current.insert(value.toMap().value(QStringLiteral("filename")).toString());
            QStringList keep;
            QSet<QString> seen;
            bool valid = true;
            for (const QVariant& value : requested) {
                if (value.metaType().id() != QMetaType::QString) { valid = false; break; }
                const QString name = value.toString();
                if (!current.contains(name) || seen.contains(name)) { valid = false; break; }
                seen.insert(name);
                keep.append(name);
            }
            if (!valid) reason = QStringLiteral("invalidRequest");
            else {
                const QVariantList favorites = m_app->favoriteProfiles();
                for (int i = static_cast<int>(favorites.size()) - 1; i >= 0; --i) {
                    if (!seen.contains(favorites.at(i).toMap().value(QStringLiteral("filename")).toString()))
                        m_app->removeFavoriteProfile(i);
                }
                m_app->setFavoriteProfileOrder(mode);
                if (mode == QLatin1String("custom")) m_app->setFavoritesOrder(keep);
                success = true;
            }
        }
    } else if (operation == QLatin1String("setAutoLoad")) {
        const QVariant filenameValue = fields.value(QStringLiteral("filename"));
        const QString autoLoad = filenameValue.toString();
        bool valid = fields.size() == 1 && fields.contains(QStringLiteral("filename"))
            && filenameValue.metaType().id() == QMetaType::QString;
        if (!autoLoad.isEmpty()) {
            valid = valid && knownFilename(autoLoad) && m_app->isFavoriteProfile(autoLoad);
        }
        if (!valid) reason = QStringLiteral("invalidRequest");
        else { m_app->setAutoLoadProfileFilename(autoLoad); success = true; }
    } else if (operation == QLatin1String("setAutoLoadRevertMinutes")) {
        const QVariant value = fields.value(QStringLiteral("minutes"));
        bool ok = false;
        const double number = value.toDouble(&ok);
        const int type = value.metaType().id();
        const bool numericType = type == QMetaType::Int || type == QMetaType::LongLong
            || type == QMetaType::Double;
        if (fields.size() != 1 || !numericType || !ok || !std::isfinite(number)
            || std::floor(number) != number || number < 0 || number > 60)
            reason = QStringLiteral("invalidRequest");
        else { m_app->setAutoLoadRevertMinutes(static_cast<int>(number)); success = true; }
    } else if (!knownFilename(filename)) {
        reason = QStringLiteral("profileNotFound");
    } else if (operation == QLatin1String("load")) {
        success = fields.isEmpty() && m_profiles->loadProfile(filename);
        if (!success) reason = QStringLiteral("loadFailed");
    } else if (operation == QLatin1String("delete")) {
        success = fields.isEmpty() && m_profiles->deleteProfile(filename);
        if (!success) reason = QStringLiteral("deleteFailed");
    } else if (operation == QLatin1String("duplicate")) {
        const QVariant titleValue = fields.value(QStringLiteral("title"));
        const QString title = titleValue.toString();
        if (fields.size() != 1 || !fields.contains(QStringLiteral("title"))
            || titleValue.metaType().id() != QMetaType::QString)
            reason = QStringLiteral("invalidRequest");
        else {
            success = m_profiles->duplicateProfile(filename, title);
            if (!success) reason = QStringLiteral("duplicateFailed");
        }
    } else if (operation == QLatin1String("rename")) {
        const QVariant titleValue = fields.value(QStringLiteral("title"));
        const QString title = titleValue.toString();
        if (fields.size() != 1 || !fields.contains(QStringLiteral("title"))
            || titleValue.metaType().id() != QMetaType::QString)
            reason = QStringLiteral("invalidRequest");
        else {
            success = m_profiles->renameProfile(filename, title);
            if (!success) reason = QStringLiteral("renameFailed");
        }
    } else if (operation == QLatin1String("toggleFavorite")) {
        if (!fields.isEmpty()) reason = QStringLiteral("invalidRequest");
        else {
            const bool desired = !m_app->isFavoriteProfile(filename);
            m_profiles->toggleFavoriteProfile(filename);
            const bool actual = m_app->isFavoriteProfile(filename);
            result.insert(QStringLiteral("favorite"), actual);
            success = actual == desired;
            if (!success) reason = QStringLiteral("operationFailed");
        }
    } else {
        reason = QStringLiteral("unsupportedOperation");
    }
    if (reason.isEmpty() && !success) reason = QStringLiteral("operationFailed");
    // Native ProfileManager emits profilesChanged synchronously for catalog
    // writes. Ensure the reply carries the post-operation revision even when
    // a no-op (for example an unchanged favorite) emitted no signal.
    if (success && operation != QLatin1String("load")) publish(true, operation == QLatin1String("delete")
        || operation == QLatin1String("duplicate") || operation == QLatin1String("rename"));
    finishOperation(requestId, operation, filename, success, reason, result);
}
