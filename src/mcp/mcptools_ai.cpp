#include "mcpserver.h"
#include "mcptoolregistry.h"
#include "../ai/dialing_blocks.h"
#include "../ai/aimanager.h"
#include "../ai/aiconversation.h"
#include "../ai/shotsummarizer.h"
#include "../ai/aiprovider.h"
#include "../controllers/maincontroller.h"
#include "../network/beanbaseclient.h"
#include "../network/beanbase_blob.h"
#include "../core/dbutils.h"
#include "../history/shothistorystorage.h"
#include "../history/coffeebagstorage.h"
#include "../history/shotprojection.h"
#include "../profile/profile.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
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
        "Invoke the configured AI advisor over a shot's dial-in context, using the provider selected "
        "in settings and no other. Returns the advisor's `response`, the assembled "
        "`systemPromptUsed`/`userPromptUsed`, and a `structuredNext` object when the reply makes a "
        "concrete recommendation. dryRun assembles the prompts without calling the provider. "
        "Errors if the advisor is already busy. Side effects and prompt overrides: get_agent_file "
        "topic \"ai_advisor_invoke\".",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"shot_id", QJsonObject{{"type", "integer"},
                    {"description", "Shot ID to build context for. If omitted, uses the most recent shot."}}},
                {"dryRun", QJsonObject{{"type", "boolean"},
                    {"description", "Assemble the prompts and return them without calling the provider. Default false"}}},
                {"userPromptOverride", QJsonObject{{"type", "string"},
                    {"description", "Replace the auto-built user prompt with custom text. Useful for prompt A/B testing."}}},
                {"systemPromptOverride", QJsonObject{{"type", "string"},
                    {"description", "Replace the auto-built system prompt. Omitted: the shot profile's standard analysis prompt"}}}
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
                DialingBlocks::AdvisorContextBlocks blocks;

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
                        // The dialing-context blocks, from the one assembler
                        // the in-app advisor also calls, so the two surfaces
                        // cannot ship different context for the same shot. The
                        // struct travels whole rather than being unpacked into
                        // one local per block: that is what kept a new block
                        // from reaching only one of the two surfaces. Loading
                        // the conversation turns stays here — see
                        // buildAdvisorContextBlocks on why it is a parameter.
                        QList<AIConversation::HistoricalAssistantTurn> turns;
                        if (!shot.profileKbId.isEmpty()) {
                            turns = AIConversation::loadRecentAssistantTurnsForKey(
                                AIManager::conversationKey(shot),
                                DialingBlocks::kRecentAdviceTurns);
                        }
                        blocks = DialingBlocks::buildAdvisorContextBlocks(
                            db, shot, resolvedShotId, turns);
                    }
                });

                QMetaObject::invokeMethod(qApp,
                    [aiPtr, shot, dryRun, userPromptOverride, systemPromptOverride,
                     resolvedShotId, blocks, respond]() {
                    if (!aiPtr) {
                        respond(QJsonObject{{"error", "App shut down before advisor call could start"}});
                        return;
                    }
                    if (!shot.isValid()) {
                        respond(QJsonObject{{"error", QString("Shot not found: %1").arg(resolvedShotId)}});
                        return;
                    }
                    // Re-fetched from the QPointer rather than reusing the outer
                    // `ai`: this runs queued on the main thread, and the manager
                    // may have been destroyed in between. Distinct name because
                    // it is a distinct fact — the outer one is the pre-gate value.
                    AIManager* aiLive = aiPtr.data();

                    // Re-check the busy gate on the main thread for live
                    // calls — between the gate above and here, the user
                    // may have triggered an in-app advisor call.
                    if (!dryRun && aiLive->isAnalyzing()) {
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
                        QJsonObject userPromptObj = aiLive->buildUserPromptObjectForShot(shot);
                        if (userPromptObj.isEmpty()) {
                            respond(QJsonObject{{"error", "Failed to assemble shot summary for shot " + QString::number(resolvedShotId)}});
                            return;
                        }
                        aiLive->enrichUserPromptObject(userPromptObj, shot, blocks);
                        userPrompt = QString::fromUtf8(
                            QJsonDocument(userPromptObj).toJson(QJsonDocument::Indented));
                    }

                    // Dry-run path: return the prompts without invoking
                    // the provider. Cost-free preview for prompt design.
                    if (dryRun) {
                        respond(QJsonObject{
                            {"shotId", static_cast<double>(resolvedShotId)},
                            {"provider", aiLive->selectedProvider()},
                            {"model", aiLive->currentModelName()},
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
                    state->timeout = new QTimer(aiLive);
                    state->timeout->setSingleShot(true);
                    state->timeout->setInterval(kAdvisorMcpTimeoutMs);

                    const QString providerId = aiLive->selectedProvider();
                    const QString modelName = aiLive->currentModelName();
                    QPointer<AIManager> aiPtrInner(aiLive);

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

                    state->successConn = QObject::connect(aiLive, &AIManager::recommendationReceived,
                        aiLive, [finalize, aiPtrInner, shot, resolvedShotId, userPrompt](
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
                                const QString convKey = AIManager::conversationKey(shot);
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
                    state->errorConn = QObject::connect(aiLive, &AIManager::errorOccurred,
                        aiLive, [finalize](const QString& error) {
                            finalize(QJsonObject{{"error", error}});
                        });
                    state->timeoutConn = QObject::connect(state->timeout, &QTimer::timeout,
                        aiLive, [finalize]() {
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
                    state->destroyedConn = QObject::connect(aiLive, &QObject::destroyed,
                        QCoreApplication::instance(), [finalize]() {
                            finalize(QJsonObject{{"error", "AI manager destroyed before advisor reply"}});
                        });

                    state->timeout->start();
                    aiLive->analyze(systemPrompt, userPrompt);
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
    // bag (apply the fields with bag action=update). Lives here (not
    // mcptools_write.cpp) because it invokes the AI like ai_advisor_invoke:
    // same control tier, and the lean write-tools test binary must not need
    // AIManager/BeanBaseClient moc symbols.
    registry->registerAsyncTool(
        "bag_extract_details",
        "Run the AI page extraction for a bag's product URL and return the extracted fields "
        "WITHOUT writing them (use bag_update to apply). Response reports the stage that ran "
        "(1 = local fetch, 2 = provider-side fetch), the provider/model, and applied/corrections. "
        "A bag with no usable link (absent or known dead) gets a product-page search instead, "
        "returned as suggestedUrl and never stored. Consumes provider tokens.",
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
                // Bag context, so the response can say what the apply rule
                // WOULD write (applied/corrections) rather than only what the
                // page said — and so the last rung has a roaster and coffee to
                // search for. Never written back here: this tool reads.
                QString blob;
                QString roaster;
                QString coffee;
                AIManager* aiManager = nullptr;
                bool searchingForPage = false;
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
            st->aiManager = aiManager;
            // The last rung's terminal: hand the URL back, never store it.
            st->conns << QObject::connect(aiManager, &AIManager::productPageFound, qApp,
                [st, finish, respond](const QString& token, const QString& url) {
                    if (st->done || !st->searchingForPage || token != st->url) return;
                    finish([respond, url]() {
                        respond(QJsonObject{
                            {"suggestedUrl", url},
                            {"hint", "Not stored. Re-run with url=<that URL> to extract from it, "
                                     "or bag action=update link=<that URL> to keep it."}});
                    });
                });
            st->conns << QObject::connect(aiManager, &AIManager::productPageSearchFailed, qApp,
                [st, finish, respond](const QString& token, const QString& error) {
                    if (st->done || !st->searchingForPage || token != st->url) return;
                    finish([respond, error]() {
                        respond(QJsonObject{{"error", error == QLatin1String("notFound")
                            ? QString("No product page found for that coffee")
                            : QString("Product-page search failed: %1").arg(error)}});
                    });
                });
            st->conns << QObject::connect(aiManager, &AIManager::bagDetailsExtracted, qApp,
                [st, aiManager, finish, respond](const QString& token, const QVariantMap& fields) {
                    if (st->done || token != st->url) return;
                    const auto outcome = BeanBaseBlob::applyExtraction(st->blob, fields);
                    const QJsonObject result{
                        {"stage", st->stage},
                        {"provider", aiManager->selectedProvider()},
                        {"model", aiManager->currentModelName()},
                        {"kind", st->kind},
                        {"url", st->url},
                        // What the shared apply rule would write to THIS bag:
                        // empty fields filled, Bean-Base-sourced values the page
                        // contradicts corrected, user-typed values untouched.
                        // Advisory — the tool does not write.
                        {"applied", QJsonObject::fromVariantMap(outcome.applied)},
                        {"corrections", QJsonArray::fromVariantList(outcome.corrections)},
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
                        // A link known dead takes the search's route, like no
                        // link at all. An explicit urlOverride is the caller's
                        // own choice and is never second-guessed.
                        if (urlOverride.isEmpty()
                            && !BeanBaseBlob::linkIsUsable(bag.beanBaseData, link))
                            link.clear();
                    }
                    QMetaObject::invokeMethod(qApp, [st, beanbase, opened, valid = bag.isValid(),
                                                     tea, link, finish, respond, bagId,
                                                     blob = bag.beanBaseData,
                                                     roaster = bag.roasterName,
                                                     coffee = bag.coffeeName]() {
                        st->blob = blob;
                        st->roaster = roaster;
                        st->coffee = coffee;
                        if (!opened) {
                            finish([respond]() { respond(QJsonObject{{"error", "Could not open bag database"}}); });
                            return;
                        }
                        if (!valid) {
                            finish([respond]() { respond(QJsonObject{{"error", "Bag not found"}}); });
                            return;
                        }
                        if (link.isEmpty()) {
                            // The ladder's last rung, offered rather than taken:
                            // ask the selected provider to FIND the page and
                            // hand the URL back. Nothing is stored — a model's
                            // guess becomes a bag's link only when a person
                            // says so, here as in the app.
                            AIManager* ai = st->aiManager;
                            if (ai && ai->isConfigured() && ai->supportsProductPageSearch()
                                && !st->roaster.isEmpty() && !st->coffee.isEmpty()) {
                                st->searchingForPage = true;
                                ai->findProductPage(st->url = QStringLiteral("mcpfind:%1").arg(bagId),
                                                    st->roaster, st->coffee,
                                                    tea ? QStringLiteral("tea") : QStringLiteral("coffee"));
                                return;
                            }
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
        "control", McpTierNiche);

    // ai_conversations_list / ai_conversation_get — split into their own
    // translation unit (mcptools_ai_conversations.cpp) so tests can link
    // them against a real AIManager without dragging in this file's
    // MainController/ShotHistoryStorage/BeanBaseClient dependencies. Same
    // rationale as registerBeanSearchTool below.
    registerAIConversationTools(registry, mainController ? mainController->aiManager() : nullptr);

    registerBeanSearchTool(registry, mainController ? mainController->beanbase() : nullptr);
}
