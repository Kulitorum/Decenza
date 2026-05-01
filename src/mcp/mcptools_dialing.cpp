#include "mcpserver.h"
#include "mcptoolregistry.h"
#include "mcptools_dialing_helpers.h"
#include "../history/shothistorystorage.h"
#include "../controllers/maincontroller.h"
#include "../controllers/profilemanager.h"
#include "../ai/aimanager.h"
#include "../ai/shotsummarizer.h"
#include "../core/settings.h"
#include "../core/settings_dye.h"
#include "../profile/profile.h"

#include <QDateTime>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QThread>
#include <QMetaObject>
#include <QCoreApplication>

#include "../core/dbutils.h"

// Data collected on the background thread (pure SQL results, no QObject access)
struct DialingDbResult {
    ShotProjection shotData;
    QString profileKbId;
    QJsonArray dialInSessions;
    QJsonObject grinderContext;
};

// Build the changeFromPrev diff between two adjacent shots in the same
// session — same shape as `shots_compare` produces, computed inline so the
// AI doesn't need a separate round-trip to see what moved.
static QJsonObject changeFromPrev(const ShotProjection& prev, const ShotProjection& curr)
{
    QJsonObject diff;
    auto diffStr = [&](const QString& a, const QString& b, const QString& key) {
        if (!a.isEmpty() && !b.isEmpty() && a != b)
            diff[key] = QString("%1 -> %2").arg(a, b);
    };
    auto diffNum = [&](double a, double b, const QString& key, const QString& unit) {
        if (a != 0 && b != 0 && qAbs(a - b) > 0.01)
            diff[key] = QString("%1 -> %2 %3 (%4%5)")
                .arg(a, 0, 'f', 1).arg(b, 0, 'f', 1).arg(unit)
                .arg(b > a ? "+" : "").arg(b - a, 0, 'f', 1);
    };
    diffStr(prev.grinderSetting, curr.grinderSetting, "grinderSetting");
    diffStr(prev.beanBrand, curr.beanBrand, "beanBrand");
    diffNum(prev.doseWeightG, curr.doseWeightG, "doseG", "g");
    diffNum(prev.finalWeightG, curr.finalWeightG, "yieldG", "g");
    diffNum(prev.durationSec, curr.durationSec, "durationSec", "s");
    diffNum(prev.enjoyment0to100, curr.enjoyment0to100, "enjoyment0to100", "");
    return diff;
}

void registerDialingTools(McpToolRegistry* registry, MainController* mainController,
                          ProfileManager* profileManager,
                          ShotHistoryStorage* shotHistory, Settings* settings)
{
    // dialing_get_context
    registry->registerAsyncTool(
        "dialing_get_context",
        "Get dial-in context: dial-in history grouped into sessions (runs of "
        "shots on the same profile within ~60 minutes of each other, with within-session "
        "changeFromPrev diffs), profile knowledge for the current shot's profile, bean/grinder "
        "metadata, grinder context (observed settings range, step size, and burr-swappable flag), "
        "and a tastingFeedback block flagging whether the shot has enjoyment / notes / refractometer "
        "data — when any is missing the block carries a recommendation to ask the user before "
        "suggesting changes. "
        "Primary read tool for dial-in conversations — a single call gives everything needed to analyze "
        "a shot and suggest changes. Default profileKnowledge contains only the current profile's "
        "curated KB entry (~1 KB); pass includeFullKnowledge: true to also receive the dial-in system "
        "prompt, reference tables, and the cross-profile catalog (~18 KB total — useful at session start "
        "but redundant on later turns). "
        "Grinder settings are shown as the user entered them — may be numbers, letters, click counts, or "
        "grinder-specific notation like Eureka multi-turn (1+4 = 1 rotation + position 4). The "
        "grinderContext block shows the range and step size observed in the user's own shot history.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"shot_id", QJsonObject{{"type", "integer"}, {"description", "Specific shot ID to analyze. If omitted, uses most recent shot."}}},
                {"history_limit", QJsonObject{{"type", "integer"}, {"description", "Number of prior shots with same profile to include (default 5, max 20)"}}},
                {"includeFullKnowledge", QJsonObject{{"type", "boolean"}, {"description", "Include the full dial-in system prompt, reference tables, and profile catalog in profileKnowledge. Default false — return only the current profile's KB entry. Useful at session start; redundant per call."}}}
            }}
        },
        [mainController, profileManager, shotHistory, settings](const QJsonObject& args, std::function<void(QJsonObject)> respond) {
            if (!shotHistory || !shotHistory->isReady()) {
                respond(QJsonObject{{"error", "Shot history not available"}});
                return;
            }

            int historyLimit = qBound(1, args["history_limit"].toInt(5), 20);
            const bool includeFullKnowledge = args.value("includeFullKnowledge").toBool();

            // Resolve shot ID on the main thread (lastSavedShotId is a simple getter)
            qint64 shotId = args["shot_id"].toInteger(0);
            if (shotId <= 0)
                shotId = shotHistory->lastSavedShotId();

            const QString dbPath = shotHistory->databasePath();

            QThread* thread = QThread::create(
                [dbPath, shotId, historyLimit, includeFullKnowledge, mainController, profileManager, settings, respond]() {
                // --- All SQL runs on this background thread ---
                DialingDbResult dbResult;

                qint64 resolvedShotId = shotId;

                // If no shot saved this session, query DB for most recent
                if (resolvedShotId <= 0) {
                    withTempDb(dbPath, "mcp_dialing_latest", [&](QSqlDatabase& db) {
                        QSqlQuery q(db);
                        if (q.exec("SELECT id FROM shots ORDER BY timestamp DESC LIMIT 1") && q.next())
                            resolvedShotId = q.value(0).toLongLong();
                    });
                }

                if (resolvedShotId <= 0) {
                    QMetaObject::invokeMethod(qApp, [respond]() {
                        respond(QJsonObject{{"error", "No shots available"}});
                    }, Qt::QueuedConnection);
                    return;
                }

                withTempDb(dbPath, "mcp_dialing", [&](QSqlDatabase& db) {
                    ShotRecord record = ShotHistoryStorage::loadShotRecordStatic(db, resolvedShotId);
                    dbResult.shotData = ShotHistoryStorage::convertShotRecord(record);
                    dbResult.profileKbId = record.profileKbId;

                    // --- Dial-in history grouped into sessions (same profile family) ---
                    // History returns DESC (newest first). Within a session
                    // we want ASC order so changeFromPrev reads "older ->
                    // newer" — matching how the user iterates. Sessions
                    // themselves stay newest-first so the most relevant
                    // recent iteration is at the top of the list.
                    if (!dbResult.profileKbId.isEmpty()) {
                        QVariantList history = ShotHistoryStorage::loadRecentShotsByKbIdStatic(db, dbResult.profileKbId, historyLimit, resolvedShotId);

                        QList<ShotProjection> shots;
                        shots.reserve(history.size());
                        for (const auto& v : history)
                            shots.append(ShotProjection::fromVariantMap(v.toMap()));

                        // Per-shot serializer. Identity fields
                        // (`grinderBrand`, `grinderModel`, `grinderBurrs`,
                        // `beanBrand`, `beanType`) are hoisted to the
                        // session-level `context` object; per-shot entries
                        // emit them only as overrides when the shot's
                        // value differs from the session context. The
                        // hoisted-field set is computed once per session
                        // (see hoistSessionContext call below) and the
                        // override values are passed into this lambda
                        // alongside the ShotProjection.
                        auto shotToJson = [](const ShotProjection& shot,
                                             const McpDialingHelpers::ShotIdentity& override) {
                            QJsonObject h;
                            h["id"] = shot.id;
                            h["timestamp"] = shot.timestampIso;
                            h["profileName"] = shot.profileName;
                            h["doseG"] = shot.doseWeightG;
                            h["yieldG"] = shot.finalWeightG;
                            h["durationSec"] = shot.durationSec;
                            h["enjoyment0to100"] = shot.enjoyment0to100 > 0
                                ? QJsonValue(shot.enjoyment0to100)
                                : QJsonValue(QJsonValue::Null);
                            h["grinderSetting"] = shot.grinderSetting;
                            // Identity overrides — emit only when this
                            // shot's value differs from session context.
                            if (!override.grinderBrand.isEmpty())
                                h["grinderBrand"] = override.grinderBrand;
                            if (!override.grinderModel.isEmpty())
                                h["grinderModel"] = override.grinderModel;
                            if (!override.grinderBurrs.isEmpty())
                                h["grinderBurrs"] = override.grinderBurrs;
                            if (!override.beanBrand.isEmpty())
                                h["beanBrand"] = override.beanBrand;
                            if (!override.beanType.isEmpty())
                                h["beanType"] = override.beanType;
                            h["notes"] = shot.espressoNotes;
                            if (shot.temperatureOverrideC > 0)
                                h["temperatureOverrideC"] = shot.temperatureOverrideC;

                            // For shots saved by MainController, targetWeightG is always
                            // populated (user override → profile target_weight → finalWeight).
                            // The profile-JSON fallback below is defensive for shots imported
                            // from external formats (de1app, visualizer.coffee) where the
                            // shot importer leaves targetWeight at 0.
                            if (shot.targetWeightG > 0) {
                                h["targetWeightG"] = shot.targetWeightG;
                            } else if (!shot.profileJson.isEmpty()) {
                                QJsonObject profileObj = QJsonDocument::fromJson(shot.profileJson.toUtf8()).object();
                                QJsonValue tw = profileObj["target_weight"];
                                double twVal = tw.isString() ? tw.toString().toDouble() : tw.toDouble();
                                if (twVal > 0)
                                    h["targetWeightG"] = twVal;
                            }
                            return h;
                        };

                        // Group the DESC-ordered shots into sessions using
                        // the pure helper (unit-tested separately).
                        QList<qint64> timestamps;
                        timestamps.reserve(shots.size());
                        for (const auto& s : shots)
                            timestamps.append(s.timestamp);
                        const auto sessionIndices = McpDialingHelpers::groupSessions(timestamps);

                        for (const auto& indices : sessionIndices) {
                            // Reverse to ASC within the session so
                            // changeFromPrev reads "older -> newer".
                            QList<ShotProjection> ordered;
                            ordered.reserve(indices.size());
                            for (qsizetype i = indices.size() - 1; i >= 0; --i)
                                ordered.append(shots[indices[i]]);

                            // Hoist common shot-identity fields to a
                            // session-level `context`. Per-shot entries
                            // carry overrides only when they differ from
                            // context. See openspec change
                            // optimize-dialing-context-payload, task 1.
                            QList<McpDialingHelpers::ShotIdentity> identities;
                            identities.reserve(ordered.size());
                            for (const ShotProjection& s : ordered) {
                                McpDialingHelpers::ShotIdentity id;
                                id.grinderBrand = s.grinderBrand;
                                id.grinderModel = s.grinderModel;
                                id.grinderBurrs = s.grinderBurrs;
                                id.beanBrand = s.beanBrand;
                                id.beanType = s.beanType;
                                identities.append(id);
                            }
                            const McpDialingHelpers::HoistedSession hoisted =
                                McpDialingHelpers::hoistSessionContext(identities);

                            QJsonArray sessionShots;
                            for (qsizetype i = 0; i < ordered.size(); ++i) {
                                QJsonObject h = shotToJson(ordered[i], hoisted.perShotOverrides[i]);
                                if (i > 0) {
                                    QJsonObject diff = changeFromPrev(ordered[i-1], ordered[i]);
                                    h["changeFromPrev"] = diff.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(diff);
                                } else {
                                    h["changeFromPrev"] = QJsonValue(QJsonValue::Null);
                                }
                                sessionShots.append(h);
                            }

                            QJsonObject contextObj;
                            if (!hoisted.context.grinderBrand.isEmpty())
                                contextObj["grinderBrand"] = hoisted.context.grinderBrand;
                            if (!hoisted.context.grinderModel.isEmpty())
                                contextObj["grinderModel"] = hoisted.context.grinderModel;
                            if (!hoisted.context.grinderBurrs.isEmpty())
                                contextObj["grinderBurrs"] = hoisted.context.grinderBurrs;
                            if (!hoisted.context.beanBrand.isEmpty())
                                contextObj["beanBrand"] = hoisted.context.beanBrand;
                            if (!hoisted.context.beanType.isEmpty())
                                contextObj["beanType"] = hoisted.context.beanType;

                            QJsonObject sessionObj;
                            sessionObj["sessionStart"] = ordered.first().timestampIso;
                            sessionObj["sessionEnd"] = ordered.last().timestampIso;
                            sessionObj["shotCount"] = static_cast<int>(ordered.size());
                            if (!contextObj.isEmpty())
                                sessionObj["context"] = contextObj;
                            sessionObj["shots"] = sessionShots;
                            dbResult.dialInSessions.append(sessionObj);
                        }
                    }

                    // --- Grinder context (shared helper) ---
                    // Per openspec optimize-dialing-context-payload (task 7):
                    // `settingsObserved` is bean-scoped — filtered to shots
                    // on the resolved shot's `beanBrand`. Cross-bean
                    // settings used to surface "you've used grind 9 before"
                    // recommendations when 9 was on a different bean
                    // entirely. When the bean-scoped query returns < 2
                    // distinct settings (the user just switched beans),
                    // also emit `allBeansSettings` (cross-bean) so the AI
                    // sees the user's overall range — explicitly tagged
                    // so it's not misread as bean-specific.
                    QString grinderModel = dbResult.shotData.grinderModel;
                    QString beverageType = dbResult.shotData.beverageType.isEmpty()
                        ? QStringLiteral("espresso") : dbResult.shotData.beverageType;
                    if (!grinderModel.isEmpty()) {
                        const QString beanBrand = dbResult.shotData.beanBrand;
                        GrinderContext ctx = ShotHistoryStorage::queryGrinderContext(
                            db, grinderModel, beverageType, beanBrand);

                        // Cross-bean fallback for sparse OR empty
                        // bean-scoped results. The pre-PR shape always
                        // populated grinderContext from the unscoped
                        // query; tightening to bean-scoped now would
                        // strand users whose resolved shot has a bean
                        // brand never used elsewhere (imported shots,
                        // novel-bean first shot before save) — the bean-
                        // scoped query returns 0 rows and they'd lose
                        // grinderContext entirely. Compute the fallback
                        // up front whenever we filtered by bean, so it's
                        // available for both the sparse (size < 2) and
                        // empty (size == 0) paths below.
                        bool haveCrossBean = false;
                        GrinderContext crossBean;
                        if (!beanBrand.isEmpty() && ctx.settingsObserved.size() < 2) {
                            crossBean = ShotHistoryStorage::queryGrinderContext(
                                db, grinderModel, beverageType);
                            haveCrossBean = !crossBean.settingsObserved.isEmpty();
                        }

                        if (!ctx.settingsObserved.isEmpty() || haveCrossBean) {
                            QJsonObject grinderCtx;
                            // When bean-scoped is empty but cross-bean
                            // has data, fall back to the cross-bean ctx
                            // for the primary settingsObserved + range
                            // fields so the AI gets the user's overall
                            // range (the closest available signal). The
                            // bean-scoped values stay empty / absent.
                            const GrinderContext& primary =
                                ctx.settingsObserved.isEmpty() ? crossBean : ctx;
                            grinderCtx["model"] = primary.model;
                            grinderCtx["beverageType"] = primary.beverageType;
                            QJsonArray settingsArr;
                            for (const auto& s : primary.settingsObserved)
                                settingsArr.append(s);
                            grinderCtx["settingsObserved"] = settingsArr;
                            grinderCtx["isNumeric"] = primary.allNumeric;
                            if (primary.allNumeric && primary.maxSetting > primary.minSetting) {
                                grinderCtx["minSetting"] = primary.minSetting;
                                grinderCtx["maxSetting"] = primary.maxSetting;
                                grinderCtx["smallestStep"] = primary.smallestStep;
                            }
                            // When bean-scoped had at least one row, also
                            // surface allBeansSettings so the AI sees
                            // the cross-bean range — explicitly tagged.
                            // Skip when bean-scoped was empty (the
                            // cross-bean values are already in
                            // settingsObserved as the fallback primary).
                            if (haveCrossBean && !ctx.settingsObserved.isEmpty()) {
                                QJsonArray allArr;
                                for (const auto& s : crossBean.settingsObserved)
                                    allArr.append(s);
                                grinderCtx["allBeansSettings"] = allArr;
                            }
                            dbResult.grinderContext = grinderCtx;
                        }
                    }
                });

                // --- Deliver results to main thread for final assembly ---
                // Main-thread work: settings access, AI analysis, profile info
                QMetaObject::invokeMethod(qApp,
                    [respond, dbResult, resolvedShotId, includeFullKnowledge, mainController, profileManager, settings]() {

                    if (!dbResult.shotData.isValid()) {
                        respond(QJsonObject{{"error", "Shot not found: " + QString::number(resolvedShotId)}});
                        return;
                    }

                    QJsonObject result;
                    auto now = QDateTime::currentDateTime();
                    result["currentDateTime"] = now.toOffsetFromUtc(now.offsetFromUtc()).toString(Qt::ISODate);
                    result["shotId"] = resolvedShotId;

                    if (!dbResult.dialInSessions.isEmpty())
                        result["dialInSessions"] = dbResult.dialInSessions;
                    if (!dbResult.grinderContext.isEmpty())
                        result["grinderContext"] = dbResult.grinderContext;

                    // --- Resolved shot reference (used by tasting + bean blocks below) ---
                    // Note: result["shot"] is intentionally NOT emitted (see openspec
                    // change optimize-dialing-context-payload, task 2). The fields it
                    // used to carry — profileName, doseG, yieldG, durationSec, ratio,
                    // grinder, bean, roastLevel, notes, enjoyment — are rendered in
                    // shotAnalysis prose, which is the single canonical surface for
                    // resolved-shot summary metadata. Shipping both forced consumers
                    // to pick a canonical version when precisions / framings differed.
                    const auto& sd = dbResult.shotData;

                    // --- Tasting feedback completeness ---
                    // Surface structural booleans so the AI knows whether
                    // taste/measurement data is present before suggesting
                    // changes. Per openspec optimize-dialing-context-payload
                    // (task 4), the per-call `recommendation` framing
                    // string is moved to the system prompt's "How to read
                    // structured fields" section — taught once per
                    // conversation. The boolean fields stay; the AI reads
                    // the "ask first when all are false" gate from the
                    // system prompt.
                    QJsonObject tastingFeedback;
                    tastingFeedback["hasEnjoymentScore"] = sd.enjoyment0to100 > 0;
                    tastingFeedback["hasNotes"] = !sd.espressoNotes.trimmed().isEmpty();
                    tastingFeedback["hasRefractometer"] = sd.drinkTdsPct > 0;
                    result["tastingFeedback"] = tastingFeedback;

                    // --- AI-generated shot analysis ---
                    if (mainController && mainController->aiManager()) {
                        AIManager* ai = mainController->aiManager();
                        QString analysis = ai->generateHistoryShotSummary(dbResult.shotData);
                        if (!analysis.isEmpty())
                            result["shotAnalysis"] = analysis;
                    }

                    // --- Profile knowledge ---
                    // Default: ship only the current profile's KB section
                    // (~1 KB). The full system prompt + reference tables +
                    // profile catalog (~18 KB total) is opt-in via
                    // includeFullKnowledge — useful at session start, but
                    // redundant on later turns of a multi-call dial-in
                    // conversation. See #987.
                    QString profileTitle = sd.profileName;
                    QString bevType = sd.beverageType.isEmpty() ? QStringLiteral("espresso") : sd.beverageType;
                    // Extract the editor type from the embedded profile JSON
                    // so the fuzzy-match fallback can find a section for
                    // custom-titled D-Flow / A-Flow profiles whose stored
                    // profileKbId is stale or absent. Without this, the
                    // fallback only matches on title alone.
                    QString profileType;
                    if (!sd.profileJson.isEmpty()) {
                        const QJsonObject pj = QJsonDocument::fromJson(sd.profileJson.toUtf8()).object();
                        profileType = pj.value("type").toString();
                    }
                    QString profileKnowledge;
                    if (includeFullKnowledge) {
                        profileKnowledge = ShotSummarizer::shotAnalysisSystemPrompt(
                            bevType, profileTitle, profileType, dbResult.profileKbId);
                    } else {
                        profileKnowledge = ShotSummarizer::profileKnowledgeForKbId(dbResult.profileKbId);
                        if (profileKnowledge.isEmpty())
                            profileKnowledge = ShotSummarizer::findProfileSection(profileTitle, profileType);
                    }
                    if (!profileKnowledge.isEmpty())
                        result["profileKnowledge"] = profileKnowledge;

                    // --- Bean/grinder metadata (current DYE settings) ---
                    // DYE wins per-field; for grinder + dose, fall back to
                    // the resolved shot's value when DYE is blank so the AI
                    // doesn't lose its highest-leverage signal (burr
                    // geometry, grinder brand, current setting) when the
                    // user hasn't filled out DYE recently. Bean fields
                    // (brand/type/roastLevel) and roastDate are *not*
                    // inferred — beans rotate per hopper and roastDate
                    // lives only inside `beanFreshness` to keep the
                    // freshness surface in one place. Merge logic lives in
                    // McpDialingHelpers::buildCurrentBean (pure,
                    // unit-tested).
                    if (settings) {
                        McpDialingHelpers::CurrentBeanInputs in;
                        in.dyeBeanBrand = settings->dye()->dyeBeanBrand();
                        in.dyeBeanType = settings->dye()->dyeBeanType();
                        in.dyeRoastLevel = settings->dye()->dyeRoastLevel();
                        in.dyeGrinderBrand = settings->dye()->dyeGrinderBrand();
                        in.dyeGrinderModel = settings->dye()->dyeGrinderModel();
                        in.dyeGrinderBurrs = settings->dye()->dyeGrinderBurrs();
                        in.dyeGrinderSetting = settings->dye()->dyeGrinderSetting();
                        in.dyeDoseWeightG = settings->dye()->dyeBeanWeight();
                        in.fallbackGrinderBrand = sd.grinderBrand;
                        in.fallbackGrinderModel = sd.grinderModel;
                        in.fallbackGrinderBurrs = sd.grinderBurrs;
                        in.fallbackGrinderSetting = sd.grinderSetting;
                        in.fallbackDoseWeightG = sd.doseWeightG;
                        in.fallbackShotId = resolvedShotId;
                        QJsonObject bean = McpDialingHelpers::buildCurrentBean(in);
                        const QJsonObject freshness = McpDialingHelpers::buildBeanFreshness(
                            settings->dye()->dyeRoastDate());
                        if (!freshness.isEmpty())
                            bean["beanFreshness"] = freshness;
                        result["currentBean"] = bean;
                    }

                    // --- Profile (single canonical block) ---
                    // Per openspec optimize-dialing-context-payload (task 8):
                    // `result.profile` is the *only* canonical surface for
                    // profile metadata. Replaces the legacy `currentProfile`
                    // block and the prose-only `Profile:` / `Profile intent:` /
                    // `## Profile Recipe` sections in `shotAnalysis`. The
                    // intent + recipe describe the resolved SHOT's profile
                    // (read off `dbResult.shotData.profileNotes` /
                    // `profileJson` — already in memory, no extra DB query);
                    // targets describe the CURRENT profile loaded on the
                    // machine. The asymmetry is intentional — the shot is
                    // what happened, the targets are what the user can act
                    // on now.
                    if (profileManager) {
                        QJsonObject profileInfo;
                        profileInfo["filename"] = profileManager->currentProfileName();
                        profileInfo["title"] = profileManager->currentProfile().title();
                        if (!sd.profileNotes.isEmpty())
                            profileInfo["intent"] = sd.profileNotes;
                        if (!sd.profileJson.isEmpty()) {
                            const QString recipe = Profile::describeFramesFromJson(sd.profileJson);
                            if (!recipe.isEmpty())
                                profileInfo["recipe"] = recipe;
                        }
                        profileInfo["targetWeightG"] = profileManager->profileTargetWeight();
                        profileInfo["targetTemperatureC"] = profileManager->profileTargetTemperature();
                        if (profileManager->profileHasRecommendedDose())
                            profileInfo["recommendedDoseG"] = profileManager->profileRecommendedDose();
                        result["profile"] = profileInfo;
                    }

                    // Note: dial-in reference tables and profile knowledge base are now
                    // embedded in the profileKnowledge system prompt (shared with in-app AI),
                    // so they are no longer sent as separate fields here.

                    respond(result);
                }, Qt::QueuedConnection);
            });

            QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
            thread->start();
        },
        "read");
}
