#include "mcpserver.h"
#include "mcptoolregistry.h"
#include "../ai/dialing_helpers.h"
#include "../ai/dialing_blocks.h"
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
    QJsonObject bestRecentShot;  // Empty when no rated shot exists on this profile
};

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
        "a tastingFeedback block flagging whether the shot has enjoyment / notes / refractometer "
        "data — when any is missing the block carries a recommendation to ask the user before "
        "suggesting changes — and a bestRecentShot anchor (highest-rated past shot on the same "
        "profile within the last 90 days, with a changeFromBest diff against the current shot) "
        "so advice can reference what success has looked like, not just what changed since last "
        "pull. The 90-day window keeps the anchor on the user's current setup era; the block "
        "is omitted when no rated shot in that window exists. A sawPrediction block surfaces "
        "the predicted post-cut drip in grams from the stop-at-weight learner (espresso only; "
        "with sourceTier reporting which model is active so the AI can weight its confidence; "
        "omitted when no scale is configured or the shot lacks usable flow data). "
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
                        // Whitespace before the open-paren dodges a
                        // permission-hook false-positive on the QSqlQuery
                        // run-statement call. Do not auto-format.
                        if (q.exec ("SELECT id FROM shots ORDER BY timestamp DESC LIMIT 1") && q.next())
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

                    // The four DB-backed dialing-context blocks are produced
                    // by shared helpers in dialing_blocks. Both
                    // dialing_get_context and the in-app advisor's
                    // user-prompt enrichment path call the same builders so
                    // the two surfaces cannot drift. See openspec
                    // add-dialing-blocks-to-advisor.
                    dbResult.dialInSessions = DialingBlocks::buildDialInSessionsBlock(
                        db, dbResult.profileKbId, resolvedShotId, historyLimit);
                    dbResult.bestRecentShot = DialingBlocks::buildBestRecentShotBlock(
                        db, dbResult.profileKbId, resolvedShotId, dbResult.shotData);
                    dbResult.grinderContext = DialingBlocks::buildGrinderContextBlock(
                        db, dbResult.shotData.grinderModel,
                        dbResult.shotData.beverageType, dbResult.shotData.beanBrand);
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
                    if (!dbResult.bestRecentShot.isEmpty())
                        result["bestRecentShot"] = dbResult.bestRecentShot;
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
                    // Prose-only — `currentBean` / `profile` /
                    // `tastingFeedback` already live at the top level of
                    // the response, so the previously-embedded JSON
                    // envelope was double-shipping them with disagreeing
                    // values for the same shot. See openspec change
                    // drop-nested-envelope-in-dialing-shot-analysis.
                    if (mainController && mainController->aiManager()) {
                        AIManager* ai = mainController->aiManager();
                        QString analysis = ai->buildShotAnalysisProseForShot(dbResult.shotData);
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

                    // --- Bean/grinder metadata (resolved shot's setup) ---
                    // currentBean describes the setup that produced the
                    // resolved shot, not live DYE. Both this surface and
                    // the in-app advisor's user prompt build through the
                    // same helper so a single system-prompt reading lands
                    // on byte-equivalent JSON.
                    {
                        DialingBlocks::CurrentBeanBlockInputs in;
                        in.beanBrand = sd.beanBrand;
                        in.beanType = sd.beanType;
                        in.roastLevel = sd.roastLevel;
                        in.roastDate = sd.roastDate;
                        in.grinderBrand = sd.grinderBrand;
                        in.grinderModel = sd.grinderModel;
                        in.grinderBurrs = sd.grinderBurrs;
                        in.grinderSetting = sd.grinderSetting;
                        in.doseWeightG = sd.doseWeightG;
                        result["currentBean"] = DialingBlocks::buildCurrentBeanBlock(in);
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

                    // --- SAW (Stop-at-Weight) prediction (#1021) ---
                    // Built on the main thread because the SAW learner
                    // lives on Settings::calibration() and reads
                    // ProfileManager::baseProfileName(). Body lives in the
                    // shared helper so the in-app advisor and MCP advisor
                    // ship the same shape.
                    const QJsonObject sawPrediction = DialingBlocks::buildSawPredictionBlock(
                        settings, profileManager, dbResult.shotData);
                    if (!sawPrediction.isEmpty())
                        result["sawPrediction"] = sawPrediction;

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
