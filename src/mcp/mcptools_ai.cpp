#include "mcpserver.h"
#include "mcptoolregistry.h"
#include "../ai/aimanager.h"
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

// Hard cap on a single advisor call. Provider-side timeout is 60s
// (AIProvider::ANALYSIS_TIMEOUT_MS). This adds a small buffer so the MCP
// caller gets a clean "timeout" reply rather than a dangling promise if
// the provider's own timeout fires.
static constexpr int kAdvisorMcpTimeoutMs = 75 * 1000;

void registerAITools(McpToolRegistry* registry, MainController* mainController)
{
    // ai_advisor_invoke
    //
    // Tier: write — makes a paid outbound call to the configured AI
    // provider. Side effects: emits `recommendationReceived` (which the
    // in-app advisor's QML overlay listens to) and updates AIManager's
    // `lastRecommendation`. Don't fire while the user is actively using
    // the in-app advisor; the call is rejected if `isAnalyzing` is true.
    //
    // Always returns `systemPromptUsed` and `userPromptUsed` so the
    // caller can verify exactly what the provider received — that
    // visibility is the whole point for prompt A/B testing. Set
    // `dryRun: true` to assemble the prompts without sending them
    // anywhere (no network call, no side effects).
    registry->registerAsyncTool(
        "ai_advisor_invoke",
        "Invoke the configured AI advisor with the dial-in context for a shot. "
        "Builds the same system prompt and user prompt the in-app advisor would build, "
        "sends to the provider currently selected in settings (OpenAI/Anthropic/Gemini/"
        "OpenRouter/Ollama), and returns the response. Always echoes the assembled "
        "systemPromptUsed + userPromptUsed in the response so the caller can see exactly "
        "what was sent — useful for prompt A/B testing and end-to-end advisor validation. "
        "Pass dryRun: true to skip the network call and just return the assembled "
        "prompts (no side effects, no token cost). "
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

                if (resolvedShotId <= 0) {
                    withTempDb(dbPath, "mcp_advisor_latest", [&](QSqlDatabase& db) {
                        QSqlQuery q(db);
                        if (q.exec("SELECT id FROM shots ORDER BY timestamp DESC LIMIT 1") && q.next())
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
                });

                QMetaObject::invokeMethod(qApp,
                    [aiPtr, shot, dryRun, userPromptOverride, systemPromptOverride,
                     resolvedShotId, respond]() {
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
                        userPrompt = ai->generateHistoryShotSummary(shot);
                        if (userPrompt.isEmpty()) {
                            respond(QJsonObject{{"error", "Failed to assemble shot summary for shot " + QString::number(resolvedShotId)}});
                            return;
                        }
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
                    // QTimer is parented to AIManager so it's cleaned up
                    // on shutdown.
                    struct CallState {
                        bool done = false;
                        QMetaObject::Connection successConn;
                        QMetaObject::Connection errorConn;
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
                        }
                        if (state->timeout) state->timeout->stop();
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
                            finalize(QJsonObject{{"response", response}});
                        });
                    state->errorConn = QObject::connect(ai, &AIManager::errorOccurred,
                        ai, [finalize](const QString& error) {
                            finalize(QJsonObject{{"error", error}});
                        });
                    QObject::connect(state->timeout, &QTimer::timeout,
                        ai, [finalize]() {
                            finalize(QJsonObject{{"error", "Advisor call timed out after 75s"}});
                        });

                    state->timeout->start();
                    ai->analyze(systemPrompt, userPrompt);
                }, Qt::QueuedConnection);
            });

            QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
            thread->start();
        },
        "write");
}
