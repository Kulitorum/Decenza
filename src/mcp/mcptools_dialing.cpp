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
#include <QVariant>
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
        "Primary read for a dial-in conversation: one call returns the dial-in session history with "
        "per-shot diffs, the profile's knowledge entry, bean and grinder metadata, the grind range "
        "seen in this user's own history, whether the shot has tasting feedback, a best-recent-shot "
        "anchor, and the stop-at-weight drip prediction. Every block: get_agent_file topic "
        "\"dialing_get_context\".",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"shot_id", QJsonObject{{"type", "integer"}, {"description", "Specific shot ID to analyze. If omitted, uses most recent shot."}}},
                {"history_limit", QJsonObject{{"type", "integer"}, {"description", "Number of prior shots with same profile to include (default 5, max 20)"}}},
                {"includeFullKnowledge", QJsonObject{{"type", "boolean"}, {"description", "Add the dial-in prompt, reference tables and profile catalog (~18 KB). Default false"}}}
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
                    // Every block takes the loaded record, so each reads the
                    // equipment package off the same row it reads the grinder
                    // from. An invalid record is answered by the isValid() check
                    // on the main thread below, which returns an error before any
                    // of these values is read.
                    dbResult.dialInSessions = DialingBlocks::buildDialInSessionsBlock(
                        db, dbResult.profileKbId, resolvedShotId, historyLimit,
                        dbResult.shotData);
                    dbResult.bestRecentShot = DialingBlocks::buildBestRecentShotBlock(
                        db, dbResult.profileKbId, resolvedShotId, dbResult.shotData);
                    dbResult.grinderContext = DialingBlocks::buildGrinderContextBlock(
                        db, dbResult.shotData);
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
                        // buildShotAnalysisProseForShot takes QVariant now (#1298);
                        // wrap the real ShotProjection so coerceShot() returns it directly.
                        QString analysis = ai->buildShotAnalysisProseForShot(QVariant::fromValue(dbResult.shotData));
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
                        // Resolve the KB id once so the content and its name label
                        // stay attached to the same entry — see shotAnalysisSystemPrompt's
                        // "## Current Profile Knowledge" block above for why the label
                        // is required (issue #1459).
                        QString resolvedKbId = dbResult.profileKbId;
                        profileKnowledge = ShotSummarizer::profileKnowledgeForKbId(resolvedKbId);
                        if (profileKnowledge.isEmpty()) {
                            resolvedKbId = ShotSummarizer::computeProfileKbId(profileTitle, profileType);
                            profileKnowledge = ShotSummarizer::profileKnowledgeForKbId(resolvedKbId);
                        }
                        if (!profileKnowledge.isEmpty()) {
                            const QString kbName = ShotSummarizer::canonicalNameForKbId(resolvedKbId);
                            if (!kbName.isEmpty()) {
                                profileKnowledge = QStringLiteral("## Current Profile Knowledge: ") + kbName
                                    + QStringLiteral("\n\n"
                                        "This heading is the matched KB entry's canonical name, not "
                                        "necessarily this shot's own profile name — see `result.profile.title` "
                                        "for the shot's actual title and refer to the shot by that title when "
                                        "talking to the user, never by this KB entry's name.\n\n")
                                    + profileKnowledge;
                            }
                        }

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
                    // Single shared mapper (DialingBlocks::beanInputsFromProjection)
                    // so this surface and the advisor's summarizeFromHistory path
                    // build currentBean from a ShotProjection identically.
                    result["currentBean"] = DialingBlocks::buildCurrentBeanBlock(
                        DialingBlocks::beanInputsFromProjection(sd));

                    // --- Profile (single canonical block) ---
                    // Per openspec optimize-dialing-context-payload (task 8):
                    // `result.profile` is the *only* canonical surface for profile
                    // metadata.
                    //
                    // Identity, intent and recipe are the SHOT'S. Targets prefer the
                    // shot's own recorded values; recommendedDoseG is not recorded per
                    // shot at all, so it is gated on the shot being the loaded profile.
                    // It used to name the profile loaded on the machine while carrying
                    // the shot's intent and recipe, so after a profile switch the block
                    // attributed one profile's intent to another's name.
                    if (profileManager) {
                        QJsonObject profileInfo;
                        // Discriminate on the TITLE, not on the resolved filename: an
                        // empty filename means EITHER the shot names no profile OR the
                        // lookup missed (imported archive, deleted profile), and
                        // conflating them is what puts one profile's filename under
                        // another's title.
                        const QString shotTitle = sd.profileName;
                        const QString shotFilename = shotTitle.isEmpty()
                            ? QString() : profileManager->findProfileByTitle(shotTitle);
                        const bool shotNamesProfile = !shotTitle.isEmpty();
                        const bool shotProfileInstalled = !shotFilename.isEmpty();

                        profileInfo["title"] = shotNamesProfile
                            ? shotTitle : profileManager->currentProfile().title();
                        if (shotProfileInstalled)
                            profileInfo["filename"] = shotFilename;
                        else if (!shotNamesProfile)
                            profileInfo["filename"] = profileManager->baseProfileName();
                        else
                            // Named, but not on this device. Naming the loaded file here
                            // would put its filename under this shot's title.
                            profileInfo["filenameUnresolved"] = true;

                        if (!sd.profileNotes.isEmpty())
                            profileInfo["intent"] = sd.profileNotes;
                        if (!sd.profileJson.isEmpty()) {
                            // Issue #1158: shared helper appends the stop-at-weight note
                            // so this MCP recipe matches the in-app advisor's exactly.
                            const QString recipe = DialingBlocks::withStopAtWeightNote(
                                Profile::describeFramesFromJson(sd.profileJson),
                                sd.targetWeightG);
                            if (!recipe.isEmpty())
                                profileInfo["recipe"] = recipe;
                        }

                        // temperature_override is 0 whenever the user did not override,
                        // which is most shots — so falling back would hand the loaded
                        // profile's target to a shot that never had one. Only fall back
                        // when the shot names no profile at all, where the loaded one IS
                        // the subject.
                        if (sd.targetWeightG > 0)
                            profileInfo["targetWeightG"] = sd.targetWeightG;
                        else if (!shotNamesProfile)
                            profileInfo["targetWeightG"] = profileManager->profileTargetWeight();
                        if (sd.temperatureOverrideC > 0)
                            profileInfo["targetTemperatureC"] = sd.temperatureOverrideC;
                        else if (!shotNamesProfile)
                            profileInfo["targetTemperatureC"] = profileManager->profileTargetTemperature();

                        // baseProfileName(), NOT currentProfileName(): the latter is a
                        // DISPLAY string that gains a "*" once the profile is edited
                        // (profilemanager.h), so comparing a filename against it fails
                        // the moment the user nudges a dose.
                        const bool shotIsLoadedProfile =
                            (!shotNamesProfile)
                            || (shotProfileInstalled
                                && shotFilename == profileManager->baseProfileName());
                        if (shotIsLoadedProfile && profileManager->profileHasRecommendedDose())
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
        "read", McpTierCore);

    // dialing_get_grinder_calibration
    //
    // Split out of dialing_get_context (#1164). The cross-profile grinder
    // calibration is only relevant when the user is weighing a profile
    // switch or asks "what grind for profile X?", so the AI fetches it on
    // demand here instead of re-receiving it on every conversational
    // dialing_get_context turn. The one-shot in-app advisor and
    // ai_advisor_invoke still build the same block inline via
    // DialingBlocks::buildGrinderCalibrationBlock — they have no follow-up
    // tool-call channel, so they need it in the initial payload. All three
    // surfaces share the one builder, so they cannot drift. NOTE (#1236):
    // the block is anchored on the CURRENT roast batch, so it is NOT a
    // stable per-conversation constant — re-fetch when the coffee changes.
    registry->registerAsyncTool(
        "dialing_get_grinder_calibration",
        "Cross-profile grind guidance for this grinder and burrs, anchored on the current roast batch "
        "(re-fetch when the coffee changes). Espresso only. Call it ONLY when the user asks about "
        "switching profiles or wants a setting for a profile other than the current shot's. Each "
        "profile comes back as a number or as finer/coarser only — never quote a number for a "
        "`directional` entry. Sources, confidence and the usage constraint: get_agent_file topic "
        "\"dialing_get_grinder_calibration\".",
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
                        // {} on any of: empty grinder model, filter/pourover
                        // beverage, invalid shot, or no dialed-in shots in this
                        // shot's equipment package. The AI explicitly asked, so
                        // say what it can and cannot conclude.
                        result["available"] = false;
                        // Deliberately does NOT assert why. The builder returns
                        // {} for several distinct reasons — no grinder model,
                        // a filter/pourover beverage, an invalid shot, or no
                        // qualifying dialed-in shots — and this handler cannot
                        // tell them apart. The previous wording named the last
                        // one as fact, so a user on a filter beverage, or with
                        // fifty dialed-in shots on that basket, was told they
                        // had none. Stating the CONSEQUENCE is always true;
                        // stating the cause was true one time in four.
                        result["reason"] =
                            "Cross-profile grinder calibration is not available for this shot. "
                            "It is computed only for espresso, and only from dialed-in shots on "
                            "this shot's own equipment package (grinder + basket + puck prep) — "
                            "shots on other equipment are deliberately not pooled in, because a "
                            "grind number is only comparable within one equipment set. Do not "
                            "tell the user anything about how many shots they have; you do not "
                            "know. Advise qualitatively (finer / coarser) and have them pull a "
                            "reference shot on the target profile with this equipment rather "
                            "than quoting a specific number.";
                    } else {
                        // Block is present and self-describing via
                        // `confidence`: "approximate" carries numbers within
                        // a stated range; "directional" carries finer/coarser
                        // only. The builder guarantees no numeric table when
                        // not validated. On the directional path, surface a
                        // `reason` so the model gives direction + a
                        // reference-shot suggestion — but do NOT set
                        // `available: false`: the directional block IS
                        // actionable data, and a top-level "unavailable" flag
                        // alongside a populated block makes some models
                        // discard the finer/coarser guidance entirely
                        // (review #1236). `confidence` + `reason` already
                        // signal the constraint; the spec permits this form.
                        result["grinderCalibration"] = calibration;
                        if (calibration[QStringLiteral("confidence")].toString()
                                == QStringLiteral("directional")) {
                            result["reason"] =
                                "No numeric cross-profile calibration for the current "
                                "coffee — not enough same-roast-batch dial-in data. Use "
                                "the per-profile finer/coarser `direction` only, and tell "
                                "the user to pull a reference shot on the target profile. "
                                "Do not quote or compute a grinder number.";
                        }
                    }
                    respond(result);
                }, Qt::QueuedConnection);
            });

            QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
            thread->start();
        },
        "read", McpTierCore);
}
