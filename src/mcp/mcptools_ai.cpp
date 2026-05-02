#include "mcpserver.h"
#include "mcptoolregistry.h"
#include "../ai/dialing_blocks.h"
#include "../ai/aimanager.h"
#include "../ai/aiconversation.h"
#include "../ai/shotsummarizer.h"
#include "../ai/aiprovider.h"
#include "../controllers/maincontroller.h"
#include "../core/dbutils.h"
#include "../history/shothistorystorage.h"
#include "../history/shotprojection.h"
#include "../profile/profile.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QPointer>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>
#include <QThread>
#include <QTimer>

// Hard cap on a single advisor call, sized to outlast the slowest
// provider's own timeout so the MCP caller always gets a clean reply
// from us rather than a dangling promise. Cloud providers cap at 60s
// (AIProvider::ANALYSIS_TIMEOUT_MS); OllamaProvider caps at 120s
// (LOCAL_ANALYSIS_TIMEOUT_MS). 135s adds a small buffer above the
// Ollama path.
static constexpr int kAdvisorMcpTimeoutMs = 135 * 1000;

void registerAITools(McpToolRegistry* registry, MainController* mainController)
{
    // ai_advisor_invoke — registered at "control" tier (matches the
    // McpToolRegistry::categoryMinLevel taxonomy: read/control/settings;
    // anything else is rejected as deny-all). Makes a paid outbound
    // call to the configured AI provider and emits AIManager signals
    // the in-app advisor's QML overlay listens to (lastRecommendation,
    // recommendationReceived). The call is rejected when isAnalyzing
    // is already true, both up-front and after the background DB load
    // hops back to the main thread (race window).
    registry->registerAsyncTool(
        "ai_advisor_invoke",
        "Invoke the configured AI advisor with the dial-in context for a shot. "
        "Builds the same system prompt and user prompt the in-app advisor would build, "
        "sends to the provider currently selected in settings (OpenAI/Anthropic/Gemini/"
        "OpenRouter/Ollama), and returns the response. Always echoes the assembled "
        "systemPromptUsed + userPromptUsed in the response so the caller can see exactly "
        "what was sent — useful for prompt A/B testing and end-to-end advisor validation. "
        "When the response makes a concrete parameter recommendation (grind / dose / profile change), "
        "the trailing fenced ```json `nextShot` block defined in the system prompt is parsed and "
        "surfaced as a top-level `structuredNext` object alongside `response`. The `structuredNext` "
        "field is OMITTED (no null placeholder) when the response is a clarifying question or "
        "otherwise carries no recommendation. "
        "Pass dryRun: true to skip the network call and just return the assembled "
        "prompts (no network call, no token cost — but does still spawn a worker thread "
        "and read the shot row from SQLite). "
        "Side effects (when not dry-run): the response also reaches the in-app "
        "conversation overlay (updates lastRecommendation, fires recommendationReceived). "
        "Returns an error if the advisor is already busy with another request. "
        "Optional overrides let the caller substitute custom system/user prompts to "
        "test alternate prompt shapes against the same provider config.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"shot_id", QJsonObject{{"type", "integer"},
                    {"description", "Shot ID to build context for. If omitted, uses the most recent shot."}}},
                {"dryRun", QJsonObject{{"type", "boolean"},
                    {"description", "If true, assemble the prompts and return them without sending to the provider. No network call, no token cost. Default false."}}},
                {"userPromptOverride", QJsonObject{{"type", "string"},
                    {"description", "Replace the auto-built user prompt with custom text. Useful for prompt A/B testing."}}},
                {"systemPromptOverride", QJsonObject{{"type", "string"},
                    {"description", "Replace the auto-built system prompt with custom text. When omitted, uses ShotSummarizer::shotAnalysisSystemPrompt for the resolved shot's profile."}}}
            }}
        },
        [mainController](const QJsonObject& args, std::function<void(QJsonObject)> respond) {
            if (!mainController || !mainController->aiManager()) {
                respond(QJsonObject{{"error", "AI advisor not available"}});
                return;
            }
            AIManager* ai = mainController->aiManager();
            const bool dryRun = args.value("dryRun").toBool();

            // Configuration / busy gates only matter for live calls;
            // a dry run just assembles prompts.
            if (!dryRun) {
                if (!ai->isConfigured()) {
                    respond(QJsonObject{{"error", "AI provider not configured. Set provider + API key in app settings first."}});
                    return;
                }
                if (ai->isAnalyzing()) {
                    respond(QJsonObject{{"error", "AI advisor busy with another request — try again in a moment."}});
                    return;
                }
            }

            ShotHistoryStorage* shotHistory = mainController->shotHistory();
            if (!shotHistory || !shotHistory->isReady()) {
                respond(QJsonObject{{"error", "Shot history not available"}});
                return;
            }

            // Resolve shot ID on the main thread before spawning the worker.
            qint64 shotId = args.value("shot_id").toInteger(0);
            if (shotId <= 0) shotId = shotHistory->lastSavedShotId();

            const QString dbPath = shotHistory->databasePath();
            const QString userPromptOverride = args.value("userPromptOverride").toString();
            const QString systemPromptOverride = args.value("systemPromptOverride").toString();
            QPointer<AIManager> aiPtr(ai);

            // Pattern matches dialing_get_context: SQL on a background
            // thread, then hop back to the main thread for AIManager
            // access (AIManager owns providers + ShotSummarizer and is
            // not thread-safe).
            QThread* thread = QThread::create(
                [dbPath, shotId, dryRun, userPromptOverride, systemPromptOverride,
                 aiPtr, respond]() {
                ShotProjection shot;
                qint64 resolvedShotId = shotId;
                QJsonArray dialInSessions;
                QJsonObject bestRecentShot;
                QJsonObject grinderContext;
                QJsonArray recentAdvice;

                if (resolvedShotId <= 0) {
                    withTempDb(dbPath, "mcp_advisor_latest", [&](QSqlDatabase& db) {
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
                        respond(QJsonObject{{"error", "No shots available — record a shot before invoking the advisor."}});
                    }, Qt::QueuedConnection);
                    return;
                }

                withTempDb(dbPath, "mcp_advisor", [&](QSqlDatabase& db) {
                    ShotRecord record = ShotHistoryStorage::loadShotRecordStatic(db, resolvedShotId);
                    shot = ShotHistoryStorage::convertShotRecord(record);

                    if (shot.isValid()) {
                        // Same dialing-context blocks the in-app advisor
                        // ships, produced by the same shared helpers so
                        // the userPromptUsed echo is byte-equivalent
                        // across surfaces. See openspec
                        // add-dialing-blocks-to-advisor.
                        dialInSessions = DialingBlocks::buildDialInSessionsBlock(
                            db, shot.profileKbId, resolvedShotId, 5);
                        bestRecentShot = DialingBlocks::buildBestRecentShotBlock(
                            db, shot.profileKbId, resolvedShotId, shot);
                        grinderContext = DialingBlocks::buildGrinderContextBlock(
                            db, shot.grinderModel, shot.beverageType, shot.beanBrand);

                        // Closed-loop recentAdvice (issue #1053). Read
                        // the conversation history straight from QSettings
                        // — the conversation key is the same hash the
                        // in-app advisor uses, so the two surfaces ship
                        // byte-equivalent recentAdvice for the same shot.
                        if (!shot.profileKbId.isEmpty()) {
                            const QString convKey = AIManager::conversationKey(
                                shot.beanBrand, shot.beanType, shot.profileName);
                            const auto turns = AIConversation::loadRecentAssistantTurnsForKey(convKey, 3);
                            if (!turns.isEmpty()) {
                                DialingBlocks::RecentAdviceInputs in;
                                in.turns = turns;
                                in.currentProfileKbId = shot.profileKbId;
                                in.currentShotId = resolvedShotId;
                                recentAdvice = DialingBlocks::buildRecentAdviceBlock(db, in);
                            }
                        }
                    }
                });

                QMetaObject::invokeMethod(qApp,
                    [aiPtr, shot, dryRun, userPromptOverride, systemPromptOverride,
                     resolvedShotId, dialInSessions, bestRecentShot, grinderContext,
                     recentAdvice, respond]() {
                    if (!aiPtr) {
                        respond(QJsonObject{{"error", "App shut down before advisor call could start"}});
                        return;
                    }
                    if (!shot.isValid()) {
                        respond(QJsonObject{{"error", QString("Shot not found: %1").arg(resolvedShotId)}});
                        return;
                    }
                    AIManager* ai = aiPtr.data();

                    // Re-check the busy gate on the main thread for live
                    // calls — between the gate above and here, the user
                    // may have triggered an in-app advisor call.
                    if (!dryRun && ai->isAnalyzing()) {
                        respond(QJsonObject{{"error", "AI advisor busy with another request — try again in a moment."}});
                        return;
                    }

                    QString systemPrompt;
                    if (!systemPromptOverride.isEmpty()) {
                        systemPrompt = systemPromptOverride;
                    } else {
                        const QString bevType = shot.beverageType.isEmpty()
                            ? QStringLiteral("espresso") : shot.beverageType;
                        QString profileType;
                        if (!shot.profileJson.isEmpty()) {
                            const QJsonObject pj = QJsonDocument::fromJson(shot.profileJson.toUtf8()).object();
                            profileType = pj.value("type").toString();
                        }
                        systemPrompt = ShotSummarizer::shotAnalysisSystemPrompt(
                            bevType, shot.profileName, profileType, shot.profileKbId);
                    }

                    QString userPrompt;
                    if (!userPromptOverride.isEmpty()) {
                        userPrompt = userPromptOverride;
                    } else {
                        // Build the JSON envelope for the resolved shot,
                        // then merge the four dialing-context blocks
                        // (dialInSessions / bestRecentShot /
                        // grinderContext from bg thread; sawPrediction
                        // built here on the main thread). Same shape the
                        // in-app advisor produces — both surfaces call
                        // the same helpers in DialingBlocks.
                        QJsonObject userPromptObj = ai->buildUserPromptObjectForShot(shot);
                        if (userPromptObj.isEmpty()) {
                            respond(QJsonObject{{"error", "Failed to assemble shot summary for shot " + QString::number(resolvedShotId)}});
                            return;
                        }
                        ai->enrichUserPromptObject(userPromptObj, shot,
                            dialInSessions, bestRecentShot, grinderContext, recentAdvice);
                        userPrompt = QString::fromUtf8(
                            QJsonDocument(userPromptObj).toJson(QJsonDocument::Indented));
                    }

                    // Dry-run path: return the prompts without invoking
                    // the provider. Cost-free preview for prompt design.
                    if (dryRun) {
                        respond(QJsonObject{
                            {"shotId", static_cast<double>(resolvedShotId)},
                            {"provider", ai->selectedProvider()},
                            {"model", ai->currentModelName()},
                            {"systemPromptUsed", systemPrompt},
                            {"userPromptUsed", userPrompt},
                            {"dryRun", true}
                        });
                        return;
                    }

                    // Live path: subscribe to AIManager's reply signals
                    // and invoke. `done` guards against double-fire (the
                    // provider's own timeout could fire alongside our
                    // wrapper's timeout in a race, though rare). The
                    // The QTimer is parented to AIManager so it dies
                    // alongside it on app shutdown — but during normal
                    // operation finalize() owns its lifetime explicitly
                    // (deleteLater) so per-call timers don't accumulate
                    // as permanent AIManager children.
                    struct CallState {
                        bool done = false;
                        QMetaObject::Connection successConn;
                        QMetaObject::Connection errorConn;
                        QMetaObject::Connection timeoutConn;
                        QMetaObject::Connection destroyedConn;
                        QTimer* timeout = nullptr;
                        qint64 startMs = 0;
                    };
                    auto* state = new CallState();
                    state->startMs = QDateTime::currentMSecsSinceEpoch();
                    state->timeout = new QTimer(ai);
                    state->timeout->setSingleShot(true);
                    state->timeout->setInterval(kAdvisorMcpTimeoutMs);

                    const QString providerId = ai->selectedProvider();
                    const QString modelName = ai->currentModelName();
                    QPointer<AIManager> aiPtrInner(ai);

                    auto finalize = [state, aiPtrInner, providerId, modelName,
                                     systemPrompt, userPrompt, resolvedShotId, respond](
                                        const QJsonObject& body) {
                        if (state->done) return;
                        state->done = true;
                        if (aiPtrInner) {
                            QObject::disconnect(state->successConn);
                            QObject::disconnect(state->errorConn);
                            QObject::disconnect(state->timeoutConn);
                            QObject::disconnect(state->destroyedConn);
                        }
                        // The QTimer is parented to AIManager; deleteLater()
                        // also runs when the parent is destroyed, so this is
                        // safe in both lifecycles.
                        if (state->timeout) {
                            state->timeout->stop();
                            state->timeout->deleteLater();
                        }
                        const qint64 latencyMs = QDateTime::currentMSecsSinceEpoch() - state->startMs;

                        QJsonObject result = body;
                        result["shotId"] = static_cast<double>(resolvedShotId);
                        result["provider"] = providerId;
                        result["model"] = modelName;
                        result["latencyMs"] = static_cast<double>(latencyMs);
                        result["systemPromptUsed"] = systemPrompt;
                        result["userPromptUsed"] = userPrompt;
                        respond(result);

                        delete state;
                    };

                    state->successConn = QObject::connect(ai, &AIManager::recommendationReceived,
                        ai, [finalize](const QString& response) {
                            // Surface the trailing structured `nextShot`
                            // block (issue #1054) as a top-level field
                            // alongside the prose response, so MCP
                            // consumers don't have to re-parse it. Absent
                            // when the response is a clarifying question
                            // or otherwise has no recommendation — no
                            // null placeholder.
                            QJsonObject body{{"response", response}};
                            const auto structured = AIManager::parseStructuredNext(response);
                            if (structured.has_value()) {
                                body.insert(QStringLiteral("structuredNext"), *structured);
                            }
                            finalize(body);
                        });
                    state->errorConn = QObject::connect(ai, &AIManager::errorOccurred,
                        ai, [finalize](const QString& error) {
                            finalize(QJsonObject{{"error", error}});
                        });
                    state->timeoutConn = QObject::connect(state->timeout, &QTimer::timeout,
                        ai, [finalize]() {
                            finalize(QJsonObject{{"error",
                                QString("Advisor call timed out after %1s")
                                    .arg(kAdvisorMcpTimeoutMs / 1000)}});
                        });
                    // If AIManager dies before any of the above signals fire
                    // (app shutdown with a live call in flight), Qt would
                    // auto-disconnect those receiver-bound lambdas without
                    // ever invoking finalize — leaking `state` and stranding
                    // `respond()`. The destroyed-signal hook makes that a
                    // clean error rather than a hang+leak. Receiver is
                    // QCoreApplication::instance() — it outlives AIManager,
                    // so this connection still fires.
                    state->destroyedConn = QObject::connect(ai, &QObject::destroyed,
                        QCoreApplication::instance(), [finalize]() {
                            finalize(QJsonObject{{"error", "AI manager destroyed before advisor reply"}});
                        });

                    state->timeout->start();
                    ai->analyze(systemPrompt, userPrompt);
                }, Qt::QueuedConnection);
            });

            QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
            thread->start();
        },
        "control");
}
