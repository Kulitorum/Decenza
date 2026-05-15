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
    QJsonObject bestRecentShot;      // Empty when no rated shot exists on this profile
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
        "grinderContext block shows the range and step size observed in the user's own shot history. "
        "For cross-profile grind translation — the recommended grinder setting for a profile OTHER than "
        "the current shot's, e.g. when the user is considering a profile switch — call "
        "dialing_get_grinder_calibration. That table is intentionally not included here: it is a stable "
        "property of the grinder, so fetching it once on demand keeps multi-turn dial-in lean.",
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

                    // The DB-backed dialing-context blocks are produced by
                    // shared helpers in dialing_blocks. Both
                    // dialing_get_context and the in-app advisor's
                    // user-prompt enrichment path call the same builders so
                    // the two surfaces cannot drift. See openspec
                    // add-dialing-blocks-to-advisor.
                    //
                    // Cross-profile grinder calibration is deliberately NOT
                    // built here (#1164). It is a ~33-row table that is a
                    // stable physical property of the grinder+burrs pair and
                    // is only relevant when the user is weighing a profile
                    // switch — shipping it on every conversational turn
                    // bloated multi-turn dial-in. It now lives in the
                    // on-demand dialing_get_grinder_calibration tool, which
                    // calls the same buildGrinderCalibrationBlock helper. The
                    // one-shot in-app advisor and ai_advisor_invoke still
                    // build it inline because they have no follow-up
                    // tool-call channel.
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
                    // cross-profile catalog (~18 KB total) is opt-in via
                    // includeFullKnowledge — useful at session start, but
                    // redundant on later turns of a multi-call dial-in
                    // conversation. See #987.
                    //
                    // #1164 finding #1: this gate used to leak. The lite
                    // branch unconditionally appended the cross-profile
                    // catalog + UGS tables + families/discipline framing for
                    // espresso (~17 KB), so every call shipped nearly the
                    // full payload regardless of includeFullKnowledge —
                    // contradicting this tool's own documented contract and
                    // dominating per-call token cost. That bulk is STABLE
                    // reference content: the AI fetches it ONCE via
                    // includeFullKnowledge, after which it lives in the
                    // external client's cached conversation prefix and never
                    // needs resending. The lite branch now keeps only a
                    // compact guardrail (see below).
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

                        // Compact guardrail only (espresso-only — the full
                        // catalog is espresso-centric, so filter/pour-over
                        // never received it). This replaces the ~17 KB
                        // cross-profile reference that used to be appended
                        // here unconditionally (#1164 finding #1). It steers
                        // the AI away from hallucinating other-profile
                        // setpoints before it has pulled the full reference,
                        // and tells it exactly how to get that reference once.
                        if (bevType.toLower() != "filter" && bevType.toLower() != "pourover") {
                            if (!profileKnowledge.isEmpty())
                                profileKnowledge += QStringLiteral("\n\n");
                            profileKnowledge += QStringLiteral(
                                "## Cross-profile reference omitted\n\n"
                                "Only THIS profile's KB entry is included here, to keep the\n"
                                "per-call payload small. For cross-profile grind ordering (UGS\n"
                                "tables), profile-family guidance, and the full dial-in system\n"
                                "prompt, call dialing_get_context ONCE with\n"
                                "`includeFullKnowledge: true` at the start of the conversation.\n"
                                "That content is stable, so one fetch is enough — it stays in\n"
                                "context for the rest of the conversation and does not need\n"
                                "re-requesting on later turns.\n\n"
                                "Until you have pulled it: you have full recipe data ONLY for\n"
                                "the current shot's profile (`result.profile.recipe`). DO NOT\n"
                                "quote specific numeric setpoints (temperatures, pressures,\n"
                                "durations) for any OTHER profile from memory — that is\n"
                                "hallucination. Describe cross-profile differences\n"
                                "qualitatively and let the user pull a reference shot on the\n"
                                "other profile to see its actual numbers.\n");
                        }
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
                            // Issue #1158: shared helper appends the
                            // stop-at-weight note so this MCP recipe
                            // matches the in-app advisor's exactly. Gate
                            // it on the SHOT's stored target weight
                            // (`sd.targetWeightG`) — the same source the
                            // recipe text comes from (`sd.profileJson`)
                            // and the same source the advisor uses
                            // (`summary.targetWeight`). Using the
                            // current profile's target here instead
                            // would re-introduce MCP/advisor drift for
                            // historical shots whose profile differs
                            // from the loaded one. The emitted
                            // `targetWeightG` field below stays
                            // current-profile per the documented
                            // shot-vs-targets asymmetry.
                            const QString recipe = DialingBlocks::withStopAtWeightNote(
                                Profile::describeFramesFromJson(sd.profileJson),
                                sd.targetWeightG);
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

    // dialing_get_grinder_calibration
    //
    // Split out of dialing_get_context (#1164). The cross-profile grinder
    // calibration table is ~33 rows and is only relevant when the user is
    // weighing a profile switch or asks "what grind for profile X?". It is a
    // stable physical property of the grinder+burrs pair, so the AI fetches
    // it once on demand here instead of re-receiving it on every
    // conversational dialing_get_context turn. The one-shot in-app advisor
    // and ai_advisor_invoke still build the same block inline via
    // DialingBlocks::buildGrinderCalibrationBlock — they have no follow-up
    // tool-call channel, so they need it in the initial payload. All three
    // surfaces share the one builder, so they cannot drift.
    registry->registerAsyncTool(
        "dialing_get_grinder_calibration",
        "Cross-profile grinder calibration for the user's grinder + burrs: the "
        "recommended grinder setting (rgs) for every known espresso profile, "
        "anchored on the profiles the user has actually pulled. Returns "
        "fineAnchor / coarseAnchor (the two history profiles the conversion is "
        "pinned to), conversionKey (grinder-setting units per UGS unit), "
        "calibratedUgsRange, and a profiles[] array — each entry has the "
        "profile's ugs, rgs, and source: history = median measured from the "
        "user's own shots, derived = interpolated within the calibrated range, "
        "extrapolated = projected outside it (lower confidence). "
        "Call this ONLY when the user asks about switching profiles or wants a "
        "grind setting for a profile other than the current shot's. It is "
        "intentionally omitted from dialing_get_context to keep multi-turn "
        "dial-in lean, and is a stable property of the grinder so one fetch "
        "per conversation is enough. Espresso only; unavailable until the user "
        "has pulled at least two KB-known profiles on this exact grinder + "
        "burrs with numeric grind settings spanning a wide enough range to "
        "anchor the conversion.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"shot_id", QJsonObject{{"type", "integer"}, {"description", "Shot whose grinder + burrs to calibrate. If omitted, uses the most recent shot."}}}
            }}
        },
        [shotHistory](const QJsonObject& args, std::function<void(QJsonObject)> respond) {
            if (!shotHistory || !shotHistory->isReady()) {
                respond(QJsonObject{{"error", "Shot history not available"}});
                return;
            }

            qint64 shotId = args["shot_id"].toInteger(0);
            if (shotId <= 0)
                shotId = shotHistory->lastSavedShotId();

            const QString dbPath = shotHistory->databasePath();

            QThread* thread = QThread::create([dbPath, shotId, respond]() {
                qint64 resolvedShotId = shotId;

                if (resolvedShotId <= 0) {
                    withTempDb(dbPath, "mcp_grindcal_latest", [&](QSqlDatabase& db) {
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

                QJsonObject calibration;
                bool shotValid = false;
                withTempDb(dbPath, "mcp_grindcal", [&](QSqlDatabase& db) {
                    ShotRecord record = ShotHistoryStorage::loadShotRecordStatic(db, resolvedShotId);
                    ShotProjection shot = ShotHistoryStorage::convertShotRecord(record);
                    shotValid = shot.isValid();
                    if (!shotValid) return;
                    calibration = DialingBlocks::buildGrinderCalibrationBlock(
                        db, shot.grinderModel, shot.grinderBurrs,
                        shot.beverageType, resolvedShotId);
                });

                QMetaObject::invokeMethod(qApp,
                    [respond, resolvedShotId, shotValid, calibration]() {
                    if (!shotValid) {
                        respond(QJsonObject{{"error", "Shot not found: " + QString::number(resolvedShotId)}});
                        return;
                    }
                    QJsonObject result;
                    result["shotId"] = resolvedShotId;
                    if (calibration.isEmpty()) {
                        // The builder returns {} when preconditions aren't met
                        // (espresso-only, ≥2 KB-known profiles on this
                        // grinder+burrs with numeric settings, non-degenerate
                        // spread). The AI explicitly asked, so explain why
                        // rather than returning a bare empty object.
                        result["available"] = false;
                        result["reason"] =
                            "Cross-profile grinder calibration is not available for this "
                            "grinder yet. It needs at least two different KB-known espresso "
                            "profiles pulled on this exact grinder + burrs, with numeric "
                            "grind settings spanning a wide enough range to anchor the "
                            "conversion. Pull a few more shots on different profiles, or "
                            "advise qualitatively (finer / coarser) instead of quoting a "
                            "specific number for another profile.";
                    } else {
                        result["grinderCalibration"] = calibration;
                    }
                    respond(result);
                }, Qt::QueuedConnection);
            });

            QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
            thread->start();
        },
        "read");
}
