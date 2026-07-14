#include "mcpserver.h"
#include "mcptoolregistry.h"
#include "../ai/dialing_blocks.h"
#include "../ai/aimanager.h"
#include "../ai/aiconversation.h"
#include "../ai/shotsummarizer.h"
#include "../ai/aiprovider.h"
#include "../controllers/maincontroller.h"
#include "../network/beanbaseclient.h"
#include "../core/dbutils.h"
#include "../history/shothistorystorage.h"
#include "../history/coffeebagstorage.h"
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

void registerBeanSearchTool(McpToolRegistry* registry, BeanBaseClient* client);
void registerAIConversationTools(McpToolRegistry* registry, AIManager* aiManager);

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
                QJsonObject grinderCalibration;
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
                        grinderCalibration = DialingBlocks::buildGrinderCalibrationBlock(
                            db, shot.grinderModel, shot.grinderBurrs,
                            shot.beverageType, resolvedShotId);

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
                     grinderCalibration, recentAdvice, respond]() {
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
                            dialInSessions, bestRecentShot, grinderContext, recentAdvice,
                            grinderCalibration);
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
                        ai, [finalize, aiPtrInner, shot, resolvedShotId, userPrompt](
                                const QString& response) {
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

                            // Persist the turn into the conversation key
                            // so future ai_advisor_invoke / in-app advisor
                            // calls on the same bean+profile see this
                            // turn in their recentAdvice block. Without
                            // this, the MCP path was effectively silent
                            // — AIConversation was only written by the
                            // in-app advisor flow.
                            //
                            // Skip when shot identity is incomplete (no
                            // bean or no profile name): conversationKey
                            // would still hash to something, but using
                            // it as an attribution anchor is unsafe.
                            if (!shot.beanBrand.isEmpty()
                                && !shot.profileName.isEmpty()) {
                                const QString convKey = AIManager::conversationKey(
                                    shot.beanBrand, shot.beanType, shot.profileName);
                                AIConversation::appendAssistantTurnForKey(
                                    convKey, resolvedShotId,
                                    userPrompt, response, structured);
                                // Keep the live in-app conversation in
                                // sync if it has the same key loaded —
                                // otherwise its next saveToStorage will
                                // overwrite the just-written turn.
                                if (aiPtrInner && aiPtrInner->conversation()
                                    && aiPtrInner->conversation()->storageKey() == convKey) {
                                    aiPtrInner->conversation()->loadFromStorage();
                                }
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

    // bag_extract_details — drive the "Get info from page" pipeline remotely
    // (add-recipe-wizard-tea): stage 1 = local page fetch -> provider
    // extraction; stage 2 fallback = the provider fetches the URL itself via
    // its web tool (JS-rendered shops). Read-only diagnostics: returns the
    // extracted fields plus which stage/provider ran — it never writes the
    // bag (apply the fields with bag_update). Lives here (not
    // mcptools_write.cpp) because it invokes the AI like ai_advisor_invoke:
    // same control tier, and the lean write-tools test binary must not need
    // AIManager/BeanBaseClient moc symbols.
    registry->registerAsyncTool(
        "bag_extract_details",
        "Run the AI page extraction for a bag's product URL and return the extracted fields "
        "WITHOUT writing them (use bag_update to apply). Uses the bag's kind to pick the "
        "coffee or tea vocabulary. Response reports which stage ran (1 = local page fetch, "
        "2 = provider-side web fetch fallback for JS-rendered shops) and the provider/model. "
        "Requires a configured AI provider; consumes provider tokens.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"bagId", QJsonObject{{"type", "integer"}, {"description", "Bag ID (from bag_list); its link is the page"}}},
                {"url", QJsonObject{{"type", "string"}, {"description", "Override URL (defaults to the bag's link)"}}}
            }},
            {"required", QJsonArray{"bagId"}}
        },
        [mainController](const QJsonObject& args, std::function<void(QJsonObject)> respond) {
            AIManager* aiManager = mainController ? mainController->aiManager() : nullptr;
            BeanBaseClient* beanbase = mainController ? mainController->beanbase() : nullptr;
            CoffeeBagStorage* bagStorage = mainController ? mainController->bagStorage() : nullptr;
            if (!bagStorage || !aiManager || !beanbase) {
                respond(QJsonObject{{"error", "Extraction dependencies not available"}});
                return;
            }
            if (!aiManager->isConfigured()) {
                respond(QJsonObject{{"error", "No AI provider configured"}});
                return;
            }
            const qint64 bagId = args["bagId"].toInteger();
            if (bagId <= 0) {
                respond(QJsonObject{{"error", "Valid bagId is required"}});
                return;
            }
            const QString urlOverride = args["url"].toString().trimmed();

            // Chain state. Each STEP disarms after its first accept (per-step
            // flags), and the whole thing severs all connections on the first
            // terminal outcome (`done` + finish()). Both matter: bag-refresh
            // and page-fetch signals are broadcast, so a concurrent
            // requestBag/fetch for the same key would otherwise re-fire a step
            // and torpedo this run.
            struct ExtractState {
                QList<QMetaObject::Connection> conns;
                QString url;
                QString kind;
                int stage = 1;
                QString stage1Error;
                qsizetype textChars = 0;
                bool fetchArmed = false;    // pageTextReady/Failed accepted once
                bool done = false;
            };
            auto st = std::make_shared<ExtractState>();
            auto finish = [st](std::function<void()> reply) {
                if (st->done) return;
                st->done = true;
                for (const auto& c : st->conns)
                    QObject::disconnect(c);
                reply();
            };

            // Step 2a: local fetch succeeded -> stage-1 extraction. One-shot:
            // disarm so a concurrent fetch of the same URL can't re-enter.
            st->conns << QObject::connect(beanbase, &BeanBaseClient::pageTextReady, qApp,
                [st, aiManager](const QString& url, const QString& text) {
                    if (st->done || st->fetchArmed || url != st->url) return;
                    st->fetchArmed = true;
                    st->textChars = text.size();
                    aiManager->extractCoffeeBagDetails(st->url, text, st->kind);
                });

            // Step 2b: local fetch failed -> provider-side web fetch, but ONLY
            // for an empty/blocked page (the in-app dialog's rule): a bad URL
            // or a down site would just burn provider tokens on a guaranteed
            // stage-2 failure. Otherwise surface the stage-1 error.
            st->conns << QObject::connect(beanbase, &BeanBaseClient::pageTextFailed, qApp,
                [st, aiManager, finish, respond](const QString& url, const QString& error) {
                    if (st->done || st->fetchArmed || url != st->url) return;
                    st->fetchArmed = true;
                    st->stage1Error = error;
                    if (error == QLatin1String("emptyPage") && aiManager->supportsUrlExtraction()) {
                        st->stage = 2;
                        aiManager->extractCoffeeBagDetailsFromUrl(st->url, st->url, st->kind);
                    } else if (error == QLatin1String("emptyPage")) {
                        finish([respond, error]() { respond(QJsonObject{{"error",
                            QString("Page fetch returned nothing (%1) and the configured provider has "
                                    "no web-fetch tool for the stage-2 fallback").arg(error)}}); });
                    } else {
                        finish([respond, error]() { respond(QJsonObject{{"error",
                            QString("Page fetch failed: %1").arg(error)}}); });
                    }
                });

            // Terminal: extraction completed / failed (token = the URL).
            st->conns << QObject::connect(aiManager, &AIManager::bagDetailsExtracted, qApp,
                [st, aiManager, finish, respond](const QString& token, const QVariantMap& fields) {
                    if (st->done || token != st->url) return;
                    const QJsonObject result{
                        {"stage", st->stage},
                        {"provider", aiManager->selectedProvider()},
                        {"model", aiManager->currentModelName()},
                        {"kind", st->kind},
                        {"url", st->url},
                        {"stage1Error", st->stage1Error.isEmpty() ? QJsonValue() : QJsonValue(st->stage1Error)},
                        {"pageTextChars", static_cast<qint64>(st->textChars)},
                        {"fields", QJsonObject::fromVariantMap(fields)}};
                    finish([respond, result]() { respond(result); });
                });
            st->conns << QObject::connect(aiManager, &AIManager::bagDetailsExtractionFailed, qApp,
                [st, finish, respond](const QString& token, const QString& error) {
                    if (st->done || token != st->url) return;
                    const int stage = st->stage;
                    // Carry the stage-1 reason into a stage-2 failure — it is
                    // the actual root cause the caller needs, and it otherwise
                    // only rode along in the success response.
                    QString msg = QString("Extraction failed at stage %1: %2").arg(stage).arg(error);
                    if (stage == 2 && !st->stage1Error.isEmpty())
                        msg += QString(" (stage 1: %1)").arg(st->stage1Error);
                    finish([respond, msg]() { respond(QJsonObject{{"error", msg}}); });
                });

            // Step 1: resolve the bag (link + kind) on a background thread —
            // NOT via the bagReady signal. requestBag has two documented
            // no-emit paths (uninitialized storage, DB open failure); routed
            // through them the MCP caller would hang forever and the armed
            // connections would leak onto app-lifetime singletons, later
            // hijacking an in-app "Get info" for the same URL. A direct
            // withTempDb load has a guaranteed terminal (found=false on any
            // failure), exactly like bag_update.
            const QString dbPath = bagStorage->databasePath();
            QThread* loadThread = QThread::create(
                [st, beanbase, dbPath, bagId, urlOverride, finish, respond]() {
                    CoffeeBag bag;
                    const bool opened = withTempDb(dbPath, "mcp_extract_bag", [&](QSqlDatabase& db) {
                        bag = CoffeeBagStorage::loadBagStatic(db, bagId);
                    });
                    QString link = urlOverride;
                    bool tea = false;
                    if (opened && bag.isValid()) {
                        tea = bag.isTea();
                        if (link.isEmpty())
                            link = QJsonDocument::fromJson(bag.beanBaseData.toUtf8())
                                       .object().value(QStringLiteral("link")).toString();
                    }
                    QMetaObject::invokeMethod(qApp, [st, beanbase, opened, valid = bag.isValid(),
                                                     tea, link, finish, respond]() {
                        if (!opened) {
                            finish([respond]() { respond(QJsonObject{{"error", "Could not open bag database"}}); });
                            return;
                        }
                        if (!valid) {
                            finish([respond]() { respond(QJsonObject{{"error", "Bag not found"}}); });
                            return;
                        }
                        if (link.isEmpty()) {
                            finish([respond]() { respond(QJsonObject{{"error",
                                "Bag has no product URL (set one with bag_update link=...)"}}); });
                            return;
                        }
                        st->url = link;
                        st->kind = tea ? QStringLiteral("tea") : QStringLiteral("coffee");
                        beanbase->fetchPageText(st->url);
                    }, Qt::QueuedConnection);
                });
            QObject::connect(loadThread, &QThread::finished, loadThread, &QObject::deleteLater);
            loadThread->start();
        },
        "control");

    // ai_conversations_list / ai_conversation_get — split into their own
    // translation unit (mcptools_ai_conversations.cpp) so tests can link
    // them against a real AIManager without dragging in this file's
    // MainController/ShotHistoryStorage/BeanBaseClient dependencies. Same
    // rationale as registerBeanSearchTool below.
    registerAIConversationTools(registry, mainController ? mainController->aiManager() : nullptr);

    registerBeanSearchTool(registry, mainController ? mainController->beanbase() : nullptr);
}
