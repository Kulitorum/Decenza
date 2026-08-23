// tst_aimanager — conversation index, keys, and the advisor payload helpers.
//
// The hoisting this file used to pin lived in a prose renderer that no longer
// exists: both advisor surfaces now send one JSON payload, and the identity
// fields shared across a session are hoisted by
// DialingHelpers::hoistSessionContext, which tst_dialing_helpers.cpp covers
// directly. Fourteen test slots asserting `### Setup:` header text went with the
// renderer rather than being rewritten to assert the same invariant twice.

#include <QtTest>
#include "core/appsettings.h"
#include <QVariant>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QPointF>
#include <QNetworkAccessManager>
#include <QPair>
#include <QList>
#include <QString>
#include <QStandardPaths>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSettings>
#include <QSqlDatabase>
#include <QDate>

#include "ai/aimanager.h"
#include "mcp/mcpagentdocs.h"
#include "ai/aiconversation.h"
#include "ai/conversationkey.h"
#include "core/settings.h"
#include "core/settings_dye.h"
#include "core/settings_ai.h"  // settings.ai()->set*: full type for the extraction-routing tests
#include "history/shotprojection.h"
#include "history/shothistorystorage.h"
#include "history/shothistory_types.h"
#include "ai/dialing_blocks.h"
#include "mcp/mcptoolregistry.h"

// Implemented in src/mcp/mcptools_ai_conversations.cpp — split into its own
// translation unit specifically so it can be linked here against a real
// AIManager without MainController/ShotHistoryStorage/BeanBaseClient.
void registerAIConversationTools(McpToolRegistry* registry, AIManager* aiManager);

namespace {

// Build a minimal but complete ShotProjection that summarizeFromHistory
// will accept (non-zero dose / yield / duration so the block renders).
ShotProjection makeShot(qint64 id, qint64 timestamp,
                        const QString& grinderBrand,
                        const QString& grinderModel,
                        const QString& grinderBurrs,
                        const QString& grinderSetting,
                        const QString& beanBrand,
                        const QString& beanType,
                        const QString& profileName,
                        const QString& profileNotes,
                        const QString& profileJson)
{
    ShotProjection p;
    p.id = id;
    p.timestamp = timestamp;
    p.timestampIso = QDateTime::fromSecsSinceEpoch(timestamp).toString(Qt::ISODate);
    p.profileName = profileName;
    p.profileNotes = profileNotes;
    p.profileJson = profileJson;
    p.beverageType = QStringLiteral("espresso");
    p.doseWeightG = 18.0;
    p.finalWeightG = 36.0;
    p.durationSec = 30.0;
    p.grinderBrand = grinderBrand;
    p.grinderModel = grinderModel;
    p.grinderBurrs = grinderBurrs;
    p.grinderSetting = grinderSetting;
    p.beanBrand = beanBrand;
    p.beanType = beanType;
    return p;
}

// RAII guard for tests that need a guaranteed-unconfigured AI provider.
// Settings persists to a store (the PID-scoped test store under DECENZA_TESTING —
// see appsettings.h) that outlives any single test function, so a bare
// `Settings settings;` does NOT mean "no provider configured" — it means
// "whatever an earlier test in this binary last wrote". Snapshot + clear on
// construction, restore on destruction (runs even if a QVERIFY fails mid-test
// and returns early).
struct AiSettingsGuard {
    explicit AiSettingsGuard(Settings* s) : m_settings(s) {
        SettingsAI* ai = s->ai();
        m_provider = ai->aiProvider();
        m_openaiKey = ai->openaiApiKey();
        m_anthropicKey = ai->anthropicApiKey();
        m_geminiKey = ai->geminiApiKey();
        m_openrouterKey = ai->openrouterApiKey();
        m_ollamaEndpoint = ai->ollamaEndpoint();
        m_ollamaModel = ai->ollamaModel();

        ai->setAiProvider(QString());
        ai->setOpenaiApiKey(QString());
        ai->setAnthropicApiKey(QString());
        ai->setGeminiApiKey(QString());
        ai->setOpenrouterApiKey(QString());
        ai->setOllamaEndpoint(QString());
        ai->setOllamaModel(QString());
    }
    ~AiSettingsGuard() {
        SettingsAI* ai = m_settings->ai();
        ai->setAiProvider(m_provider);
        ai->setOpenaiApiKey(m_openaiKey);
        ai->setAnthropicApiKey(m_anthropicKey);
        ai->setGeminiApiKey(m_geminiKey);
        ai->setOpenrouterApiKey(m_openrouterKey);
        ai->setOllamaEndpoint(m_ollamaEndpoint);
        ai->setOllamaModel(m_ollamaModel);
    }
    Settings* m_settings;
    QString m_provider, m_openaiKey, m_anthropicKey, m_geminiKey, m_openrouterKey,
            m_ollamaEndpoint, m_ollamaModel;
};

} // namespace

class tst_AIManager : public QObject {
    Q_OBJECT

private:
    // `ai_conversations` dispatches on `action`, and a merged tool always goes
    // through the async path even when the verb it selects is synchronous.
    static QJsonObject callConversations(McpToolRegistry& registry, const QJsonObject& args,
                                         QString& err)
    {
        QJsonObject out;
        registry.callAsyncTool("ai_conversations", args, 0, err,
                               [&out](QJsonObject r) { out = r; });
        return out;
    }

private slots:
    void init() { QTest::failOnWarning(); }
    // parseBagExtraction: the "Get info" response contract — JSON possibly
    // wrapped in markdown fences, whitelisted to the blob vocabulary keys.
    void parseBagExtractionHandlesFencesWhitelistAndGarbage()
    {
        bool ok = false;
        // Plain object with an off-whitelist key and a numeric value.
        QVariantMap fields = AIManager::parseBagExtraction(
            "{\"origin\":\"Colombia\",\"tastingNotes\":\"cherry, cocoa\","
            "\"price\":\"$13.25\",\"elevation\":1900}", &ok);
        QVERIFY(ok);
        QCOMPARE(fields.value("origin").toString(), QString("Colombia"));
        QCOMPARE(fields.value("tastingNotes").toString(), QString("cherry, cocoa"));
        QCOMPARE(fields.value("elevation").toString(), QString("1900"));  // numeric survives
        QVERIFY(!fields.contains("price"));  // off-whitelist dropped

        // Markdown-fenced response.
        fields = AIManager::parseBagExtraction(
            "```json\n{\"roastLevel\":\"Medium-Dark\",\"variety\":\"75% Arabica / 25% Robusta\"}\n```", &ok);
        QVERIFY(ok);
        QCOMPARE(fields.value("roastLevel").toString(), QString("Medium-Dark"));

        // Garbage / no object.
        QVERIFY(AIManager::parseBagExtraction("Sorry, I can't help with that.", &ok).isEmpty());
        QVERIFY(!ok);
        QVERIFY(AIManager::parseBagExtraction("{not json}", &ok).isEmpty());
        QVERIFY(!ok);

        // {} is a SUCCESS with an empty map — "the page states nothing" is a
        // different user message than "couldn't read the response".
        QVERIFY(AIManager::parseBagExtraction("{}", &ok).isEmpty());
        QVERIFY(ok);

        // Array values (a frequent model deviation for tasting notes) are
        // joined; long values are capped at 500 chars.
        fields = AIManager::parseBagExtraction(
            "{\"tastingNotes\":[\"cherry\",\"cocoa\",\"plum\"],\"origin\":\"" + QString(600, 'x') + "\"}", &ok);
        QVERIFY(ok);
        QCOMPARE(fields.value("tastingNotes").toString(), QString("cherry, cocoa, plum"));
        QCOMPARE(fields.value("origin").toString().size(), 500);

        // A non-empty object yielding NO usable whitelisted values is a
        // failure, not an empty success — the AI said something we can't use.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("returned an object"));
        QVERIFY(AIManager::parseBagExtraction("{\"origin\":{\"country\":\"Ethiopia\"}}", &ok).isEmpty());
        QVERIFY(!ok);
    }

    // Tea vocabulary (add-recipe-wizard-tea): the union whitelist passes the
    // tea keys through, numeric brewing values survive (as strings, like
    // elevation above), and the coffee-only keys still coexist. The °F/cup
    // NORMALIZATION itself is the model's job (prompt contract) — what the
    // parser must guarantee is that normalized numbers arrive intact.
    void parseBagExtractionTeaKeys()
    {
        bool ok = false;
        const QVariantMap fields = AIManager::parseBagExtraction(
            "{\"teaType\":\"black\",\"origin\":\"Sri Lanka\",\"garden\":\"Kenilworth\","
            "\"cultivar\":\"TRI 2025\",\"flush\":\"Spring 2026\","
            "\"tastingNotes\":\"malty, honey\",\"brewTempC\":100,"
            "\"leafGramsPer100Ml\":0.85,\"steepTime\":\"3-5 minutes\","
            "\"price\":\"£17.95\"}", &ok);
        QVERIFY(ok);
        QCOMPARE(fields.value("teaType").toString(), QString("black"));
        QCOMPARE(fields.value("garden").toString(), QString("Kenilworth"));
        QCOMPARE(fields.value("cultivar").toString(), QString("TRI 2025"));
        QCOMPARE(fields.value("flush").toString(), QString("Spring 2026"));
        QCOMPARE(fields.value("brewTempC").toString(), QString("100"));
        QCOMPARE(fields.value("leafGramsPer100Ml").toString(), QString("0.85"));
        QCOMPARE(fields.value("steepTime").toString(), QString("3-5 minutes"));
        QVERIFY(!fields.contains("price"));

        // imageUrl is the stage-2-only channel for SPA product photos into the
        // bag-image cache — it must survive the whitelist.
        bool ok2 = false;
        const QVariantMap withImage = AIManager::parseBagExtraction(
            "{\"teaType\":\"black\",\"imageUrl\":\"https://x/tin.jpg\"}", &ok2);
        QVERIFY(ok2);
        QCOMPARE(withImage.value("imageUrl").toString(), QString("https://x/tin.jpg"));
    }

    // The extraction prompt must switch vocabulary by bag kind: a tea page
    // asked for coffee keys (roastLevel) would silently return the wrong data.
    // Drives the request builder via the friend seam (m_lastSystemPrompt).
    void extractionKindSelectsVocabulary()
    {
        QNetworkAccessManager nam;
        Settings settings;
        settings.ai()->setAiProvider("openai");
        settings.ai()->setOpenaiApiKey("sk-test");  // isConfigured() so the request builds
        AIManager mgr(&nam, &settings);

        mgr.extractCoffeeBagDetails("https://x/tea", "tea page text", "tea");
        QVERIFY(mgr.m_lastSystemPrompt.contains("teaType"));
        QVERIFY(mgr.m_lastSystemPrompt.contains("leafGramsPer100Ml"));
        QVERIFY(!mgr.m_lastSystemPrompt.contains("roastLevel"));
        mgr.m_analyzing = false;  // clear the in-flight guard for the next call

        mgr.extractCoffeeBagDetails("https://x/coffee", "coffee page text", "coffee");
        QVERIFY(mgr.m_lastSystemPrompt.contains("roastLevel"));
        QVERIFY(!mgr.m_lastSystemPrompt.contains("teaType"));
    }

    // Stable guard codes on the stage-2 URL path: notConfigured with no
    // provider, urlFetchUnsupported when the provider has no web tool
    // (ChangeBeansDialog and bag_extract_details both branch on these).
    void urlExtractionGuardCodes()
    {
        QNetworkAccessManager nam;
        Settings settings;
        AiSettingsGuard guard(&settings);  // guarantee no provider configured
        AIManager mgr(&nam, &settings);
        QSignalSpy failed(&mgr, &AIManager::bagDetailsExtractionFailed);

        mgr.extractCoffeeBagDetailsFromUrl("https://x/bag", "https://x/bag", "coffee");
        QCOMPARE(failed.count(), 1);
        QCOMPARE(failed.last().at(1).toString(), QString("notConfigured"));

        // Ollama is configured but has no server-side web tool.
        settings.ai()->setAiProvider("ollama");
        settings.ai()->setOllamaEndpoint("http://localhost:11434");
        settings.ai()->setOllamaModel("llama3");
        AIManager mgr2(&nam, &settings);
        QSignalSpy failed2(&mgr2, &AIManager::bagDetailsExtractionFailed);
        mgr2.extractCoffeeBagDetailsFromUrl("https://x/bag", "https://x/bag", "coffee");
        QCOMPARE(failed2.count(), 1);
        QCOMPARE(failed2.last().at(1).toString(), QString("urlFetchUnsupported"));
    }

    // The extraction request-type routing: ANY leak into recommendationReceived
    // renders raw JSON in the advisor UI; a stuck flag misroutes the advisor's
    // next response. Drives the private slots directly via the friend seam —
    // providers are concrete network classes with no injection point.
    void bagExtractionRoutingAndFlagReset()
    {
        QNetworkAccessManager nam;
        Settings settings;
        AIManager mgr(&nam, &settings);
        QSignalSpy extracted(&mgr, &AIManager::bagDetailsExtracted);
        QSignalSpy extractFailed(&mgr, &AIManager::bagDetailsExtractionFailed);
        QSignalSpy recommendation(&mgr, &AIManager::recommendationReceived);
        QSignalSpy advisorError(&mgr, &AIManager::errorOccurred);

        // Busy guard: synchronous failure with the "busy" code and the echoed
        // token; does not clobber the in-flight request's state.
        mgr.m_analyzing = true;
        mgr.extractCoffeeBagDetails("https://x/bag", "some page text");
        QCOMPARE(extractFailed.count(), 1);
        QCOMPARE(extractFailed.last().at(0).toString(), QString("https://x/bag"));
        QCOMPARE(extractFailed.last().at(1).toString(), QString("busy"));
        QVERIFY(mgr.m_analyzing);

        // Success routes to bagDetailsExtracted with the token — never to the
        // advisor's recommendationReceived — and consumes the flag.
        mgr.m_isBagExtractionRequest = true;
        mgr.m_bagExtractionToken = "https://x/bag";
        mgr.onAnalysisComplete("{\"origin\":\"Colombia\"}");
        QCOMPARE(extracted.count(), 1);
        QCOMPARE(extracted.last().at(0).toString(), QString("https://x/bag"));
        QCOMPARE(recommendation.count(), 0);
        QVERIFY(!mgr.m_isBagExtractionRequest);

        // Unreadable response: extraction failure ("unreadable"), still not
        // the advisor's signal.
        mgr.m_isBagExtractionRequest = true;
        mgr.m_bagExtractionToken = "https://x/bag";
        mgr.m_analyzing = true;
        mgr.onAnalysisComplete("Sorry, I cannot help with that.");
        QCOMPARE(extractFailed.count(), 2);
        QCOMPARE(extractFailed.last().at(1).toString(), QString("unreadable"));
        QCOMPARE(recommendation.count(), 0);

        // Provider error routes to bagDetailsExtractionFailed, not
        // errorOccurred, and resets the flag so the NEXT completion routes to
        // the advisor again.
        mgr.m_isBagExtractionRequest = true;
        mgr.m_bagExtractionToken = "https://x/bag";
        mgr.m_analyzing = true;
        mgr.onAnalysisFailed("timeout");
        QCOMPARE(extractFailed.count(), 3);
        QCOMPARE(advisorError.count(), 0);
        mgr.m_analyzing = true;
        mgr.onAnalysisComplete("plain advice");
        QCOMPARE(recommendation.count(), 1);  // routing restored
        QCOMPARE(extracted.count(), 1);
    }

    void initTestCase()
    {
        // Isolate the conversation index from the real user dir so loading /
        // saving doesn't mutate state outside the test.
        QStandardPaths::setTestModeEnabled(true);
    }

    // ---------------------------------------------------------------------
    // openspec add-dialing-blocks-to-advisor — user-prompt envelope
    //
    // Pins the contract that buildUserPromptObjectForShot returns the
    // canonical four-key envelope (currentBean / profile / tastingFeedback /
    // shotAnalysis) without any of the four DB-scoped enrichment keys that
    // the in-app advisor's bg-thread closure layers on. Synchronous callers
    // (`generateEmailPrompt`, `generateShotSummary`,
    // `generateHistoryShotSummary`) never see those four enrichment keys —
    // they're added by callers with DB scope, not by ShotSummarizer itself.
    // ---------------------------------------------------------------------
    void buildUserPromptObjectForShot_carriesCanonicalEnvelope()
    {
        QNetworkAccessManager nam;
        Settings settings;
        AIManager mgr(&nam, &settings);

        const ShotProjection shot = makeShot(1, QDateTime::currentSecsSinceEpoch(),
            QStringLiteral("Niche"), QStringLiteral("Zero"),
            QStringLiteral("63mm Kony"), QStringLiteral("4.0"),
            QStringLiteral("Northbound"), QStringLiteral("Spring Tour"),
            QStringLiteral("80's Espresso"), QStringLiteral("intent"), QString());

        const QJsonObject obj = mgr.buildUserPromptObjectForShot(shot);
        QVERIFY(obj.contains(QStringLiteral("currentBean")));
        QVERIFY(obj.contains(QStringLiteral("profile")));
        QVERIFY(obj.contains(QStringLiteral("tastingFeedback")));
        QVERIFY(obj.contains(QStringLiteral("shotAnalysis")));
    }

    void buildUserPromptObjectForShot_omitsDialingEnrichmentKeys()
    {
        QNetworkAccessManager nam;
        Settings settings;
        AIManager mgr(&nam, &settings);

        const ShotProjection shot = makeShot(1, QDateTime::currentSecsSinceEpoch(),
            QStringLiteral("Niche"), QStringLiteral("Zero"),
            QStringLiteral("63mm Kony"), QStringLiteral("4.0"),
            QStringLiteral("Northbound"), QStringLiteral("Spring Tour"),
            QStringLiteral("80's Espresso"), QStringLiteral("intent"), QString());

        const QJsonObject obj = mgr.buildUserPromptObjectForShot(shot);
        // The four DB-scoped enrichment keys are layered on by callers with
        // DB scope (the in-app advisor's bg-thread closure,
        // ai_advisor_invoke). They MUST NOT come from the synchronous
        // envelope builder, otherwise we'd be shipping nulls or stale data.
        QVERIFY2(!obj.contains(QStringLiteral("dialInSessions")),
                 "dialInSessions must be added by DB-scoped callers, not the envelope builder");
        QVERIFY2(!obj.contains(QStringLiteral("bestRecentShot")),
                 "bestRecentShot must be added by DB-scoped callers, not the envelope builder");
        QVERIFY2(!obj.contains(QStringLiteral("grinderContext")),
                 "grinderContext must be added by DB-scoped callers, not the envelope builder");
        QVERIFY2(!obj.contains(QStringLiteral("sawPrediction")),
                 "sawPrediction must be added by DB-scoped callers, not the envelope builder");
    }

    // Cache stability invariant: the user prompt envelope must not embed any
    // wall-clock value that varies per call. `currentDateTime` (the field
    // dialing_get_context's response carries at the top level) MUST NOT
    // appear in the user prompt — including it would bust the prompt cache
    // on every multi-turn follow-up.
    void buildUserPromptObjectForShot_omitsCurrentDateTime()
    {
        QNetworkAccessManager nam;
        Settings settings;
        AIManager mgr(&nam, &settings);

        const ShotProjection shot = makeShot(1, QDateTime::currentSecsSinceEpoch(),
            QStringLiteral("Niche"), QStringLiteral("Zero"),
            QStringLiteral("63mm Kony"), QStringLiteral("4.0"),
            QStringLiteral("Northbound"), QStringLiteral("Spring Tour"),
            QStringLiteral("80's Espresso"), QStringLiteral("intent"), QString());

        const QJsonObject obj = mgr.buildUserPromptObjectForShot(shot);
        const QString json = QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
        QVERIFY2(!obj.contains(QStringLiteral("currentDateTime")),
                 "user prompt must not carry a top-level currentDateTime key");
        QVERIFY2(!json.contains(QStringLiteral("currentDateTime")),
                 "no currentDateTime substring anywhere in serialized prompt");
    }

    // Two calls with identical state produce byte-identical envelopes —
    // load-bearing precondition for Anthropic's prompt cache to hit on
    // multi-turn follow-ups.
    void buildUserPromptObjectForShot_byteStableAcrossCalls()
    {
        QNetworkAccessManager nam;
        Settings settings;
        AIManager mgr(&nam, &settings);

        const ShotProjection shot = makeShot(42, 1700000000,
            QStringLiteral("Niche"), QStringLiteral("Zero"),
            QStringLiteral("63mm Kony"), QStringLiteral("4.0"),
            QStringLiteral("Northbound"), QStringLiteral("Spring Tour"),
            QStringLiteral("80's Espresso"), QStringLiteral("intent"), QString());

        const QString a = QString::fromUtf8(
            QJsonDocument(mgr.buildUserPromptObjectForShot(shot)).toJson(QJsonDocument::Indented));
        const QString b = QString::fromUtf8(
            QJsonDocument(mgr.buildUserPromptObjectForShot(shot)).toJson(QJsonDocument::Indented));
        QCOMPARE(a, b);
    }

    // ---------------------------------------------------------------------
    // Both surfaces produce byte-equivalent `currentBean` JSON for the
    // same resolved shot. The MCP path
    // (`dialing_get_context.currentBean`) and the in-app advisor's
    // user-prompt path
    // (`AIManager::buildUserPromptObjectForShot(...)["currentBean"]`)
    // build through the shared
    // `DialingBlocks::buildCurrentBeanBlock`, sourced solely from the
    // resolved shot. Pinned end-to-end so future drift between the two
    // builders fails the test rather than confusing the LLM with two
    // disagreeing views of the same shot.
    // ---------------------------------------------------------------------
    void currentBean_equivalenceAcrossSurfaces()
    {
        QNetworkAccessManager nam;
        // Live DYE state is deliberately divergent from the shot's saved
        // metadata to model the case where the user changed DYE between
        // pulling the shot and asking the AI about it. currentBean must
        // NOT pick up the live DYE values on either surface — the shot is
        // the source of truth.
        Settings settings;
        settings.dye()->setDyeBeanBrand(QStringLiteral("Live DYE Brand"));
        settings.dye()->setDyeBeanType(QStringLiteral("Live DYE Type"));
        settings.dye()->setDyeRoastLevel(QStringLiteral("Light"));
        settings.dye()->setDyeGrinderBrand(QStringLiteral("Live DYE Grinder"));
        settings.dye()->setDyeGrinderModel(QStringLiteral("Live DYE Model"));
        settings.dye()->setDyeGrinderBurrs(QStringLiteral("Live DYE Burrs"));
        settings.dye()->setDyeGrinderSetting(QStringLiteral("99"));
        settings.dye()->setDyeBeanWeight(99.0);
        settings.dye()->setDyeRoastDate(QStringLiteral("2025-01-01"));

        AIManager mgr(&nam, &settings);

        // Shot has its own bean / grinder / dose / roastDate that
        // currentBean must echo on every surface.
        ShotProjection shot = makeShot(884, 1700000000,
            QStringLiteral("Niche"), QStringLiteral("Zero"),
            QStringLiteral("63mm Kony"), QStringLiteral("4.5"),
            QStringLiteral("Northbound"), QStringLiteral("Spring Tour 2026 #2"),
            QStringLiteral("80's Espresso"), QStringLiteral("intent"), QString());
        shot.doseWeightG = 20.0;
        shot.roastLevel = QStringLiteral("Dark");
        shot.roastDate = QStringLiteral("2026-03-30");
        shot.rpm = 1400;  // variable-RPM grind axis must reach currentBean

        // In-app advisor surface: through ShotSummarizer::buildUserPromptObject
        // off summarizeFromHistory(shot).
        const QJsonObject inAppEnvelope = mgr.buildUserPromptObjectForShot(shot);
        QVERIFY(inAppEnvelope.contains(QStringLiteral("currentBean")));
        const QJsonObject inAppCurrentBean = inAppEnvelope.value(QStringLiteral("currentBean")).toObject();

        // MCP surface: the same shared helper that mcptools_dialing.cpp
        // calls on the resolved shot (mirrors the
        // `mcptools_dialing.cpp:200`-block exactly — same field-by-field
        // mapping from `sd` (the resolved shot) into
        // `CurrentBeanBlockInputs`).
        DialingBlocks::CurrentBeanBlockInputs in;
        in.identity.beanBrand = shot.beanBrand;
        in.identity.beanType = shot.beanType;
        in.roastLevel = shot.roastLevel;
        in.roastDate = shot.roastDate;
        in.identity.grinderBrand = shot.grinderBrand;
        in.identity.grinderModel = shot.grinderModel;
        in.identity.grinderBurrs = shot.grinderBurrs;
        in.grinderSetting = shot.grinderSetting;
        in.rpm = static_cast<int>(shot.rpm);
        in.doseWeightG = shot.doseWeightG;
        const QJsonObject mcpCurrentBean = DialingBlocks::buildCurrentBeanBlock(in);

        // The contract: byte-equivalent JSON for the same shot.
        QCOMPARE(inAppCurrentBean, mcpCurrentBean);

        // The grinder RPM reaches currentBean on both surfaces (a second grind
        // axis the advisor needs for variable-RPM grinders).
        QCOMPARE(inAppCurrentBean.value(QStringLiteral("rpm")).toInt(), 1400);

        // Spot-check the shot values won the source-of-truth contest
        // against the live DYE values, on both surfaces.
        QCOMPARE(inAppCurrentBean.value(QStringLiteral("type")).toString(),
                 QStringLiteral("Spring Tour 2026 #2"));
        QCOMPARE(inAppCurrentBean.value(QStringLiteral("roastLevel")).toString(),
                 QStringLiteral("Dark"));
        QCOMPARE(inAppCurrentBean.value(QStringLiteral("doseWeightG")).toDouble(), 20.0);

        // Inferred-field machinery is gone on both surfaces.
        QVERIFY(!inAppCurrentBean.contains(QStringLiteral("inferredFields")));
        QVERIFY(!inAppCurrentBean.contains(QStringLiteral("inferredFromShotId")));
        QVERIFY(!inAppCurrentBean.contains(QStringLiteral("inferredNote")));
        QVERIFY(!mcpCurrentBean.contains(QStringLiteral("inferredFields")));
        QVERIFY(!mcpCurrentBean.contains(QStringLiteral("inferredFromShotId")));

        // beanFreshness reads from the shot's roastDate, not live DYE's.
        QVERIFY(inAppCurrentBean.contains(QStringLiteral("beanFreshness")));
        const QJsonObject freshness =
            inAppCurrentBean.value(QStringLiteral("beanFreshness")).toObject();
        QCOMPARE(freshness.value(QStringLiteral("roastDate")).toString(),
                 QStringLiteral("2026-03-30"));
    }

    // ---------------------------------------------------------------------
    // openspec drop-nested-envelope-in-dialing-shot-analysis — pin that
    // `dialing_get_context.shotAnalysis` is prose-only (no nested JSON
    // envelope) and that the prose matches the in-app advisor's user-
    // prompt envelope's `shotAnalysis` field byte-for-byte.
    // ---------------------------------------------------------------------
    void buildShotAnalysisProseForShot_returnsProseNotJson()
    {
        QNetworkAccessManager nam;
        Settings settings;
        AIManager mgr(&nam, &settings);

        const ShotProjection shot = makeShot(1, QDateTime::currentSecsSinceEpoch(),
            QStringLiteral("Niche"), QStringLiteral("Zero"),
            QStringLiteral("63mm Kony"), QStringLiteral("4.0"),
            QStringLiteral("Northbound"), QStringLiteral("Spring Tour"),
            QStringLiteral("80's Espresso"), QStringLiteral("intent"), QString());

        const QString prose = mgr.buildShotAnalysisProseForShot(QVariant::fromValue(shot));
        QVERIFY(!prose.isEmpty());

        // Prose body — starts with the Shot Summary header, contains the
        // Phase Data block.
        QVERIFY2(prose.contains(QStringLiteral("## Shot Summary")),
                 "prose body must carry the Shot Summary header");
        QVERIFY2(prose.contains(QStringLiteral("## Phase Data")),
                 "prose body must carry the Phase Data header");

        // Not a JSON envelope — must NOT carry the structured-field
        // block names that the previous nested envelope embedded.
        QVERIFY2(!prose.contains(QStringLiteral("\"currentBean\"")),
                 "prose body must not embed a JSON currentBean block");
        QVERIFY2(!prose.contains(QStringLiteral("\"tastingFeedback\"")),
                 "prose body must not embed a JSON tastingFeedback block");
        QVERIFY2(!prose.contains(QStringLiteral("\"profile\":")),
                 "prose body must not embed a JSON profile block");

        // Parsing the prose as JSON should not yield an object — it's a
        // markdown string, not a JSON-encoded envelope.
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(prose.toUtf8(), &err);
        QVERIFY2(err.error != QJsonParseError::NoError || !doc.isObject(),
                 "prose body must not parse as a JSON object");
    }

    void buildShotAnalysisProseForShot_matchesEnvelopeShotAnalysisField()
    {
        QNetworkAccessManager nam;
        Settings settings;
        AIManager mgr(&nam, &settings);

        const ShotProjection shot = makeShot(42, 1700000000,
            QStringLiteral("Niche"), QStringLiteral("Zero"),
            QStringLiteral("63mm Kony"), QStringLiteral("4.0"),
            QStringLiteral("Northbound"), QStringLiteral("Spring Tour"),
            QStringLiteral("80's Espresso"), QStringLiteral("intent"), QString());

        // The prose returned by buildShotAnalysisProseForShot MUST be the
        // same string the user-prompt envelope carries under its
        // `shotAnalysis` key — they share the private renderer, and any
        // future drift would re-introduce the bug this change retired.
        const QString prose = mgr.buildShotAnalysisProseForShot(QVariant::fromValue(shot));
        const QJsonObject envelope = mgr.buildUserPromptObjectForShot(shot);
        const QString envelopeShotAnalysis = envelope.value(QStringLiteral("shotAnalysis")).toString();

        QCOMPARE(prose, envelopeShotAnalysis);
    }

    // ---------------------------------------------------------------------
    // enrichUserPromptObject — single-source merge step shared by the in-app
    // advisor and ai_advisor_invoke. Pins that the four blocks land at the
    // right keys, that empty blocks are suppressed (no nulls), and that the
    // merged envelope is byte-stable across calls.
    // ---------------------------------------------------------------------
    void enrichUserPromptObject_mergesAllFourBlocks()
    {
        QNetworkAccessManager nam;
        Settings settings;
        AIManager mgr(&nam, &settings);

        const ShotProjection shot = makeShot(1, 1700000000,
            QStringLiteral("Niche"), QStringLiteral("Zero"),
            QStringLiteral("63mm Kony"), QStringLiteral("4.0"),
            QStringLiteral("Northbound"), QStringLiteral("Spring Tour"),
            QStringLiteral("80's Espresso"), QStringLiteral("intent"), QString());

        QJsonObject payload = mgr.buildUserPromptObjectForShot(shot);

        // Synthetic blocks — the merge step is what's under test, not the
        // bg-thread DB builders. SAW is omitted by the helper (no flow data
        // on this minimal shot), which is the correct behavior.
        const QJsonArray dialInSessions{
            QJsonObject{{"sessionStart", "2026-04-29T09:29:19-06:00"},
                        {"shotCount", 1}}};
        const QJsonObject bestRecentShot{{"id", 42}, {"enjoyment0to100", 85}};
        const QJsonObject grinderContext{{"model", "Zero"}, {"stepSize", 0.25}};

        DialingBlocks::AdvisorContextBlocks blocks;
        blocks.dialInSessions = dialInSessions;
        blocks.bestRecentShot = bestRecentShot;
        blocks.grinderContext = grinderContext;
        mgr.enrichUserPromptObject(payload, shot, blocks);

        QVERIFY(payload.contains(QStringLiteral("dialInSessions")));
        QVERIFY(payload.contains(QStringLiteral("bestRecentShot")));
        QVERIFY(payload.contains(QStringLiteral("grinderContext")));
        // SAW correctly suppressed — no flow data on a synthetic ShotProjection.
        QVERIFY2(!payload.contains(QStringLiteral("sawPrediction")),
                 "SAW must be suppressed when ShotProjection has no usable flow data");

        // Original four-key envelope still intact under the new keys.
        QVERIFY(payload.contains(QStringLiteral("currentBean")));
        QVERIFY(payload.contains(QStringLiteral("profile")));
        QVERIFY(payload.contains(QStringLiteral("tastingFeedback")));
        QVERIFY(payload.contains(QStringLiteral("shotAnalysis")));
    }

    void enrichUserPromptObject_suppressesEmptyBlocks()
    {
        QNetworkAccessManager nam;
        Settings settings;
        AIManager mgr(&nam, &settings);

        const ShotProjection shot = makeShot(1, 1700000000,
            QStringLiteral("Niche"), QStringLiteral("Zero"),
            QStringLiteral("63mm Kony"), QStringLiteral("4.0"),
            QStringLiteral("Northbound"), QStringLiteral("Spring Tour"),
            QStringLiteral("80's Espresso"), QStringLiteral("intent"), QString());

        QJsonObject payload = mgr.buildUserPromptObjectForShot(shot);

        // All blocks empty — none of the four enrichment keys should appear,
        // and crucially no `null` placeholders. dialing_get_context's omission
        // contract requires absent key, not `null`.
        mgr.enrichUserPromptObject(payload, shot, DialingBlocks::AdvisorContextBlocks{});

        QVERIFY2(!payload.contains(QStringLiteral("dialInSessions")),
                 "empty dialInSessions must not be added as a key");
        QVERIFY2(!payload.contains(QStringLiteral("bestRecentShot")),
                 "empty bestRecentShot must not be added as a key");
        QVERIFY2(!payload.contains(QStringLiteral("grinderContext")),
                 "empty grinderContext must not be added as a key");
        QVERIFY2(!payload.contains(QStringLiteral("sawPrediction")),
                 "empty sawPrediction must not be added as a key");

        // Serialized output also free of the keys (no `null`-shaped JSON).
        const QString json = QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact));
        QVERIFY(!json.contains(QStringLiteral("dialInSessions")));
        QVERIFY(!json.contains(QStringLiteral("bestRecentShot")));
        QVERIFY(!json.contains(QStringLiteral("grinderContext")));
        QVERIFY(!json.contains(QStringLiteral("sawPrediction")));
    }

    void enrichUserPromptObject_byteStableAcrossCalls()
    {
        QNetworkAccessManager nam;
        Settings settings;
        AIManager mgr(&nam, &settings);

        const ShotProjection shot = makeShot(42, 1700000000,
            QStringLiteral("Niche"), QStringLiteral("Zero"),
            QStringLiteral("63mm Kony"), QStringLiteral("4.0"),
            QStringLiteral("Northbound"), QStringLiteral("Spring Tour"),
            QStringLiteral("80's Espresso"), QStringLiteral("intent"), QString());

        const QJsonArray dialInSessions{QJsonObject{{"shotCount", 2}}};
        const QJsonObject bestRecentShot{{"id", 7}};
        const QJsonObject grinderContext{{"model", "Zero"}};

        QJsonObject a = mgr.buildUserPromptObjectForShot(shot);
        DialingBlocks::AdvisorContextBlocks blocks;
        blocks.dialInSessions = dialInSessions;
        blocks.bestRecentShot = bestRecentShot;
        blocks.grinderContext = grinderContext;
        mgr.enrichUserPromptObject(a, shot, blocks);

        QJsonObject b = mgr.buildUserPromptObjectForShot(shot);
        mgr.enrichUserPromptObject(b, shot, blocks);

        const QString jsonA = QString::fromUtf8(QJsonDocument(a).toJson(QJsonDocument::Indented));
        const QString jsonB = QString::fromUtf8(QJsonDocument(b).toJson(QJsonDocument::Indented));
        QCOMPARE(jsonA, jsonB);
    }

    void enrichUserPromptObject_mergesRecentAdviceWhenPopulated()
    {
        // Mirrors the four-block test pattern. A populated recentAdvice
        // array must be merged into the envelope under the recentAdvice
        // key.
        QNetworkAccessManager nam;
        Settings settings;
        AIManager mgr(&nam, &settings);

        const ShotProjection shot = makeShot(1, 1700000000,
            QStringLiteral("Niche"), QStringLiteral("Zero"),
            QStringLiteral("63mm Kony"), QStringLiteral("4.0"),
            QStringLiteral("Northbound"), QStringLiteral("Spring Tour"),
            QStringLiteral("80's Espresso"), QStringLiteral("intent"), QString());

        QJsonObject payload = mgr.buildUserPromptObjectForShot(shot);

        const QJsonArray recentAdvice{
            QJsonObject{{"turnsAgo", 1},
                        {"recommendation", "Try grinder 4.75"},
                        {"structuredNext", QJsonObject{{"grinderSetting", "4.75"}}},
                        {"userResponse", QJsonObject{{"adherence", "followed"},
                                                      {"outcomeRating0to100", 75}}}}
        };

        DialingBlocks::AdvisorContextBlocks blocks;
        blocks.recentAdvice = recentAdvice;
        mgr.enrichUserPromptObject(payload, shot, blocks);

        QVERIFY(payload.contains(QStringLiteral("recentAdvice")));
        QCOMPARE(payload.value("recentAdvice").toArray().size(), 1);
        const QJsonObject entry = payload.value("recentAdvice").toArray().first().toObject();
        QCOMPARE(entry.value("turnsAgo").toInt(), 1);
    }

    void enrichUserPromptObject_suppressesRecentAdviceWhenEmpty()
    {
        QNetworkAccessManager nam;
        Settings settings;
        AIManager mgr(&nam, &settings);

        const ShotProjection shot = makeShot(1, 1700000000,
            QStringLiteral("Niche"), QStringLiteral("Zero"),
            QStringLiteral("63mm Kony"), QStringLiteral("4.0"),
            QStringLiteral("Northbound"), QStringLiteral("Spring Tour"),
            QStringLiteral("80's Espresso"), QStringLiteral("intent"), QString());

        QJsonObject payload = mgr.buildUserPromptObjectForShot(shot);
        // Explicit empty recentAdvice → key omitted (no `recentAdvice: []`
        // placeholder), matching the dialing_get_context omission contract.
        mgr.enrichUserPromptObject(payload, shot, DialingBlocks::AdvisorContextBlocks{});

        QVERIFY2(!payload.contains(QStringLiteral("recentAdvice")),
                 "empty recentAdvice must not be added as a key");
        const QString json = QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact));
        QVERIFY2(!json.contains(QStringLiteral("recentAdvice")),
                 "serialized envelope must not carry an empty recentAdvice array");
    }

    // ---------------------------------------------------------------------
    // DialingBlocks gating — preconditions that short-circuit before
    // touching the DB / Settings / ProfileManager. These cases ship the
    // omission contract (empty QJsonObject so callers suppress the key)
    // without needing real DB infrastructure.
    // ---------------------------------------------------------------------
    void sawPredictionBlock_omittedWhenSettingsNull()
    {
        // Espresso shot WITH flow data so we get past the espresso and flow
        // gates, then assert the settings-null gate fires.
        ShotProjection shot = makeShot(1, 1700000000,
            QStringLiteral("Niche"), QStringLiteral("Zero"),
            QStringLiteral("63mm"), QStringLiteral("4.0"),
            QStringLiteral("Bean"), QStringLiteral("Type"),
            QStringLiteral("Profile"), QString(), QString());
        shot.flow = QVariantList{
            QVariantMap{{"x", 28.0}, {"y", 1.8}},
            QVariantMap{{"x", 29.0}, {"y", 2.0}},
            QVariantMap{{"x", 30.0}, {"y", 2.1}}};
        const QJsonObject sp = DialingBlocks::buildSawPredictionBlock(nullptr, nullptr, shot);
        QVERIFY(sp.isEmpty());
    }

    void sawPredictionBlock_omittedForNonEspresso()
    {
        // Provide flow data so the flow gate would not fire — the
        // beverage-type gate must be what produces the empty result.
        ShotProjection shot = makeShot(1, 1700000000,
            QStringLiteral("Niche"), QStringLiteral("Zero"),
            QStringLiteral("63mm"), QStringLiteral("4.0"),
            QStringLiteral("Bean"), QStringLiteral("Type"),
            QStringLiteral("Profile"), QString(), QString());
        shot.beverageType = QStringLiteral("filter");
        shot.flow = QVariantList{
            QVariantMap{{"x", 28.0}, {"y", 4.0}},
            QVariantMap{{"x", 29.0}, {"y", 4.2}},
            QVariantMap{{"x", 30.0}, {"y", 4.1}}};
        Settings settings;
        const QJsonObject sp = DialingBlocks::buildSawPredictionBlock(&settings, nullptr, shot);
        QVERIFY(sp.isEmpty());
    }

    void sawPredictionBlock_omittedWhenFlowAtCutoffIsZero()
    {
        // Espresso shot, empty flow samples → estimateFlowAtCutoff returns
        // 0 → flow gate fires before the settings/profileManager gates can
        // be evaluated.
        const ShotProjection shot = makeShot(1, 1700000000,
            QStringLiteral("Niche"), QStringLiteral("Zero"),
            QStringLiteral("63mm"), QStringLiteral("4.0"),
            QStringLiteral("Bean"), QStringLiteral("Type"),
            QStringLiteral("Profile"), QString(), QString());
        Settings settings;
        const QJsonObject sp = DialingBlocks::buildSawPredictionBlock(&settings, nullptr, shot);
        QVERIFY(sp.isEmpty());
    }

    void dialInSessionsBlock_returnsEmpty_whenProfileKbIdEmpty()
    {
        // Pass an unopened DB ref — the empty-kbId guard short-circuits
        // before any DB access. (We can't easily stand up a real DB here;
        // this test pins the gating, not the DB query path.)
        QSqlDatabase db; // default-constructed: invalid, never used
        const QJsonArray arr = DialingBlocks::buildDialInSessionsBlock(
            db, QString(), AdviceScope(0), 1, 5);
        QVERIFY(arr.isEmpty());
    }

    void bestRecentShotBlock_returnsEmpty_whenProfileKbIdEmpty()
    {
        QSqlDatabase db;
        ShotProjection shot;
        const QJsonObject obj = DialingBlocks::buildBestRecentShotBlock(
            db, QString(), AdviceScope(0), 1, shot);
        QVERIFY(obj.isEmpty());
    }

    void grinderContextBlock_returnsEmpty_whenGrinderModelEmpty()
    {
        QSqlDatabase db;
        const QJsonObject obj = DialingBlocks::buildGrinderContextBlock(
            db, QString(), AdviceScope(0), QStringLiteral("espresso"), QString());
        QVERIFY(obj.isEmpty());
    }

    // Builds one recentAdvice entry matching buildRecentAdviceBlock's shape
    // (dialing_blocks.cpp) so the renderer tests below exercise the exact
    // field set the real block builder emits.
    static QJsonObject makeRecentAdviceEntry(int turnsAgo, const QString& adherence,
                                              int outcomeRating = -1)
    {
        QJsonObject sn;
        sn["grinderSetting"] = QStringLiteral("4.75");
        sn["expectedDurationSec"] = QJsonArray{ 32, 38 };
        sn["expectedFlowMlPerSec"] = QJsonArray{ 1.0, 1.5 };
        sn["successCondition"] = QStringLiteral("durationSec in [32,38]");
        sn["reasoning"] = QStringLiteral("Slow flow toward profile target");

        QJsonObject resp;
        resp["actualNextShotId"] = 105;
        resp["grinderSetting"] = QStringLiteral("4.75");
        resp["doseG"] = 18.0;
        resp["adherence"] = adherence;
        if (outcomeRating >= 0)
            resp["outcomeRating0to100"] = outcomeRating;
        QJsonObject inRange;
        inRange["duration"] = true;
        inRange["flow"] = false;
        resp["outcomeInPredictedRange"] = inRange;

        QJsonObject entry;
        entry["turnsAgo"] = turnsAgo;
        entry["recommendation"] = sn.value("reasoning").toString();
        entry["structuredNext"] = sn;
        entry["userResponse"] = resp;
        return entry;
    }

    // =====================================================================
    // AIConversation::extractShotFields — issue #1039
    // Pins the structured-field migration: dose / yield / duration /
    // grinder / score / notes are read from the JSON payload's `shot`,
    // `currentBean` and `profile` blocks. The content is the payload and
    // nothing else — no prose header, no trailing question — so this is a
    // plain QJsonDocument::fromJson over the whole string.
    //
    // Friend-class access (`friend class tst_AIManager` under
    // DECENZA_TESTING) lets these tests reach the private static
    // helper without instantiating an AIConversation.
    // =====================================================================
    void aiConversation_extractShotFields_structuredEnvelope_readsCanonicalKeys()
    {
        const QString content = QStringLiteral(
            "{"
            "  \"shotLabel\": \"2026-05-01 14:30\","
            "  \"question\": \"Please analyze.\","
            "  \"currentBean\": {"
            "    \"grinderBrand\": \"Niche\","
            "    \"grinderModel\": \"Zero\","
            "    \"grinderBurrs\": \"63mm\","
            "    \"doseWeightG\": 18.0"
            "  },"
            "  \"profile\": {\"title\": \"80's Espresso\"},"
            "  \"shot\": {"
            "    \"doseG\": 18.0,"
            "    \"yieldG\": 36.0,"
            "    \"durationSec\": 30.0,"
            "    \"grinderSetting\": \"4.0\","
            "    \"enjoyment0to100\": 85,"
            "    \"notes\": \"balanced\""
            "  },"
            "  \"shotAnalysis\": \"## Shot Summary\\n- Dose: 18g, etc.\""
            "}");

        const auto fields = AIConversation::extractShotFields(content);
        QCOMPARE(fields.shotLabel, QStringLiteral("2026-05-01 14:30"));
        QCOMPARE(fields.doseG, QStringLiteral("18.0"));
        QCOMPARE(fields.yieldG, QStringLiteral("36.0"));
        QCOMPARE(fields.durationSec, QStringLiteral("30"));
        QCOMPARE(fields.score, QStringLiteral("85"));
        QCOMPARE(fields.notes, QStringLiteral("balanced"));
        QCOMPARE(fields.profileTitle, QStringLiteral("80's Espresso"));
        // Format mirrors the legacy prose ("<brand> <model> with <burrs>
        // @ <setting>") so cross-era conversations (one shot's grinder
        // captured by regex from prose, the next from JSON) do not
        // emit spurious "grinder changed" diffs.
        QCOMPARE(fields.grinder, QStringLiteral("Niche Zero with 63mm @ 4.0"));
    }

    // Regression: AIConversation stamps internal per-turn keys (shotId from
    // #1053 shot latching, structuredNext) onto stored message objects. Those
    // are the same objects handed to the provider, and the Anthropic Messages
    // API 400s on unknown per-message fields ("messages.0.shotId: Extra inputs
    // are not permitted"), which broke every dial-in conversation request.
    // sanitizeApiMessages must strip everything but role + content.
    void sanitizeApiMessages_stripsInternalPerTurnKeys()
    {
        QJsonArray messages;

        QJsonObject user;
        user["role"] = QStringLiteral("user");
        user["content"] = QStringLiteral("How did this shot taste?");
        user["shotId"] = 1113.0;  // stored as double, per addUserMessage
        messages.append(user);

        QJsonObject assistant;
        assistant["role"] = QStringLiteral("assistant");
        assistant["content"] = QStringLiteral("Grind finer by one step.");
        assistant["shotId"] = 1113.0;
        assistant["structuredNext"] = QJsonObject{{"grindDelta", -1}};
        messages.append(assistant);

        // A future/unknown internal key must be stripped too. The sanitizer is
        // a whitelist (rebuilds each object from role + content), so this guards
        // against a later refactor to an explicit-blacklist approach that would
        // silently leak any newly-stamped bookkeeping field.
        QJsonObject withUnknownKey;
        withUnknownKey["role"] = QStringLiteral("user");
        withUnknownKey["content"] = QStringLiteral("Anything else?");
        withUnknownKey["someFutureInternalKey"] = 42;
        messages.append(withUnknownKey);

        const QJsonArray clean = AIManager::sanitizeApiMessages(messages);

        QCOMPARE(clean.size(), 3);
        for (const QJsonValue& v : clean) {
            const QJsonObject msg = v.toObject();
            // Exactly the two API-legal keys — nothing else may survive.
            QCOMPARE(msg.keys().size(), 2);
            QVERIFY(msg.contains(QStringLiteral("role")));
            QVERIFY(msg.contains(QStringLiteral("content")));
            QVERIFY(!msg.contains(QStringLiteral("shotId")));
            QVERIFY(!msg.contains(QStringLiteral("structuredNext")));
            QVERIFY(!msg.contains(QStringLiteral("someFutureInternalKey")));
        }
        // Content and role are preserved verbatim.
        QCOMPARE(clean.at(0).toObject().value("role").toString(), QStringLiteral("user"));
        QCOMPARE(clean.at(0).toObject().value("content").toString(),
                 QStringLiteral("How did this shot taste?"));
        QCOMPARE(clean.at(1).toObject().value("role").toString(), QStringLiteral("assistant"));
    }

    void aiConversation_extractShotFields_detectorFlagsEchoFromShotAnalysisProse()
    {
        // Use the actual production-emitted strings from
        // ShotAnalysis::analyzeShot — "Sustained channeling detected"
        // (lowercase "c" in "channeling"). This is what the deterministic
        // detector pipeline writes into summaryLines.text, and the
        // substring matcher in extractShotFields is tuned to it.
        const QString content = QStringLiteral(
            "{"
            "  \"shot\": {\"doseG\": 18.0, \"yieldG\": 36.0},"
            "  \"shotAnalysis\": \"## Shot Summary\\n- [warning] Sustained channeling detected in dC/dt\","
            "  \"question\": \"What to do?\""
            "}");

        const auto fields = AIConversation::extractShotFields(content);
        QVERIFY(fields.channelingDetected);
    }

    // After issue #1037, the structured `shot.detectorObservations[]`
    // array is the canonical surface for detector flags. extractShotFields
    // prefers it over substring-searching the prose body — even when the
    // shotAnalysis prose says the opposite.
    void aiConversation_extractShotFields_detectorFlagsPreferStructuredArray()
    {
        const QString content = QStringLiteral(
            "{"
            "  \"shot\": {"
            "    \"doseG\": 18.0,"
            "    \"detectorObservations\": ["
            "      {\"type\": \"warning\", \"text\": \"Sustained channeling detected in dC/dt\"}"
            "    ]"
            "  },"
            "  \"shotAnalysis\": \"## Shot Summary\\nNo issues observed.\""
            "}");

        const auto fields = AIConversation::extractShotFields(content);
        QVERIFY2(fields.channelingDetected,
                 "detectorObservations[] takes precedence over the prose body");
    }

    // The structured-array path's stable `kind` enum is the canonical
    // signal — it's robust against future rewordings of the
    // human-readable `text` field. Issue #1037: pin that contract.
    void aiConversation_extractShotFields_readsByStableKindEnum()
    {
        // `text` is rewritten so substring matching would fail; only the
        // `kind` enum carries the signal. The flag must still set.
        const QString content = QStringLiteral(
            "{"
            "  \"shot\": {"
            "    \"doseG\": 18.0,"
            "    \"detectorObservations\": ["
            "      {\"type\": \"warning\", \"kind\": \"channeling_sustained\","
            "       \"text\": \"PUCK PREP ISSUE — totally rewritten in a future release\"}"
            "    ]"
            "  }"
            "}");

        const auto fields = AIConversation::extractShotFields(content);
        QVERIFY2(fields.channelingDetected,
                 "kind=channeling_sustained must set channelingDetected even when text drifts");
    }

    // Transient channeling also sets the flag (kind=channeling_transient).
    void aiConversation_extractShotFields_transientChannelingKindAlsoSetsFlag()
    {
        const QString content = QStringLiteral(
            "{"
            "  \"shot\": {"
            "    \"detectorObservations\": ["
            "      {\"type\": \"caution\", \"kind\": \"channeling_transient\","
            "       \"text\": \"Transient channel at 14s (self-healed)\"}"
            "    ]"
            "  }"
            "}");
        const auto fields = AIConversation::extractShotFields(content);
        QVERIFY(fields.channelingDetected);
    }

    // Pre-#1037 envelopes ship `text` without `kind`. Substring fallback
    // against the production text string (`channeling detected`) keeps
    // those envelopes working.
    void aiConversation_extractShotFields_kindAbsentFallsBackToTextSubstring()
    {
        const QString content = QStringLiteral(
            "{"
            "  \"shot\": {"
            "    \"detectorObservations\": ["
            "      {\"type\": \"warning\", \"text\": \"Sustained channeling detected in dC/dt\"}"
            "    ]"
            "  }"
            "}");
        const auto fields = AIConversation::extractShotFields(content);
        QVERIFY(fields.channelingDetected);
    }

    void aiConversation_extractShotFields_normalizesNumericPrecision()
    {
        const QString content = QStringLiteral(
            "{\"shot\": {\"doseG\": 18, \"yieldG\": 36, \"durationSec\": 27}}");
        const auto fields = AIConversation::extractShotFields(content);
        QCOMPARE(fields.doseG, QStringLiteral("18.0"));
        QCOMPARE(fields.yieldG, QStringLiteral("36.0"));
        QCOMPARE(fields.durationSec, QStringLiteral("27"));
    }

    void aiConversation_extractShotFields_emptyShotProducesEmptyFields()
    {
        const QString content = QStringLiteral("{\"shotAnalysis\": \"## Shot Summary\\n\"}");
        const auto fields = AIConversation::extractShotFields(content);
        QVERIFY(fields.doseG.isEmpty());
        QVERIFY(fields.yieldG.isEmpty());
        QVERIFY(fields.durationSec.isEmpty());
        QVERIFY(fields.score.isEmpty());
        QVERIFY(fields.notes.isEmpty());
    }

    // -------------------------------------------------------------
    // Structured nextShot parser (issue #1054)
    // -------------------------------------------------------------

    void parseStructuredNext_extractsTrailingBlock()
    {
        const QString message = QStringLiteral(
            "Try going slightly finer to slow extraction toward the profile target.\n\n"
            "```json\n"
            "{\n"
            "  \"grinderSetting\": \"4.75\",\n"
            "  \"expectedDurationSec\": [32, 38],\n"
            "  \"expectedFlowMlPerSec\": [1.0, 1.5],\n"
            "  \"successCondition\": \"durationSec in [32,38] AND flowMlPerSec in [1.0,1.5]\",\n"
            "  \"reasoning\": \"Slow flow toward profile target without going past the choke point\"\n"
            "}\n"
            "```");

        const auto parsed = AIManager::parseStructuredNext(message);
        QVERIFY2(parsed.has_value(), "trailing json block should parse");
        const QJsonObject obj = *parsed;
        QCOMPARE(obj.value("grinderSetting").toString(), QStringLiteral("4.75"));
        QCOMPARE(obj.value("expectedDurationSec").toArray().size(), 2);
        QCOMPARE(obj.value("expectedDurationSec").toArray()[0].toInt(), 32);
        QCOMPARE(obj.value("expectedDurationSec").toArray()[1].toInt(), 38);
        QCOMPARE(obj.value("expectedFlowMlPerSec").toArray().size(), 2);
        QVERIFY(!obj.value("successCondition").toString().isEmpty());
        QVERIFY(!obj.value("reasoning").toString().isEmpty());
    }

    void parseStructuredNext_toleratesTrailingWhitespace()
    {
        const QString message = QStringLiteral(
            "Recommend a finer grind.\n\n"
            "```json\n"
            "{\"grinderSetting\":\"4.75\",\"expectedDurationSec\":[32,38],"
            "\"expectedFlowMlPerSec\":[1.0,1.5],"
            "\"successCondition\":\"OK\",\"reasoning\":\"r\"}\n"
            "```\n\n   \n");
        const auto parsed = AIManager::parseStructuredNext(message);
        QVERIFY(parsed.has_value());
        QCOMPARE(parsed->value("grinderSetting").toString(), QStringLiteral("4.75"));
    }

    void parseStructuredNext_caseInsensitiveTag()
    {
        const QString message = QStringLiteral(
            "advice\n\n```JSON\n{\"grinderSetting\":\"4.75\"}\n```");
        const auto parsed = AIManager::parseStructuredNext(message);
        QVERIFY(parsed.has_value());
        QCOMPARE(parsed->value("grinderSetting").toString(), QStringLiteral("4.75"));
    }

    void parseStructuredNext_returnsNulloptOnAbsentBlock()
    {
        const QString message = QStringLiteral(
            "How did this shot taste? Please give a 1-100 score and 1-2 lines of notes.");
        const auto parsed = AIManager::parseStructuredNext(message);
        QVERIFY(!parsed.has_value());
    }

    void parseStructuredNext_returnsNulloptOnMidMessageBlock()
    {
        // A mid-message json block (e.g., the model echoing prior advice
        // for context) MUST NOT be picked up — only a trailing block
        // qualifies. This message has a json block, then prose after it.
        const QString message = QStringLiteral(
            "Earlier I suggested:\n"
            "```json\n{\"grinderSetting\":\"4.75\"}\n```\n"
            "Now let's reconsider. Here's what I think went wrong: ...");
        const auto parsed = AIManager::parseStructuredNext(message);
        QVERIFY2(!parsed.has_value(),
                 "mid-message json blocks must not be extracted as the trailing recommendation");
    }

    void parseStructuredNext_ignoresNonJsonTrailingFence()
    {
        const QString message = QStringLiteral(
            "advice\n\n"
            "```python\nprint('hi')\n```");
        const auto parsed = AIManager::parseStructuredNext(message);
        QVERIFY(!parsed.has_value());
    }

    void parseStructuredNext_returnsNulloptOnMalformedJson()
    {
        // Broken JSON: unterminated brace, unquoted key. Parser must log
        // a warning and return nullopt — caller must not see a partial
        // structuredNext object. Pin the expected qWarning per TESTING.md
        // so a silent failure of the warning path would fail the test.
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("AIManager::parseStructuredNext: structuredNext parse failed.*"));
        const QString message = QStringLiteral(
            "advice\n\n```json\n{grinderSetting: 4.75\n```");
        const auto parsed = AIManager::parseStructuredNext(message);
        QVERIFY(!parsed.has_value());
    }

    void parseStructuredNext_emptyAndWhitespaceOnly()
    {
        QVERIFY(!AIManager::parseStructuredNext(QString()).has_value());
        QVERIFY(!AIManager::parseStructuredNext(QStringLiteral("   \n\n  ")).has_value());
    }

    void parseStructuredNext_oddFenceCountStillExtractsTrailingBlock()
    {
        // A stray ``` somewhere in the prose (model truncation, escaped
        // example, inline-code mishap) MUST NOT silently drop a
        // structurally valid trailing block. Total fence count here is
        // 3 (one orphan + opener+closer of the trailing block), which
        // an earlier draft of the parser bailed on.
        const QString message = QStringLiteral(
            "I noticed your earlier response truncated mid-fence ```\n"
            "but here's a fresh recommendation:\n\n"
            "```json\n{\"grinderSetting\":\"4.75\"}\n```");
        const auto parsed = AIManager::parseStructuredNext(message);
        QVERIFY2(parsed.has_value(),
                 "trailing valid block must parse even when an earlier stray ``` makes the total count odd");
        QCOMPARE(parsed->value("grinderSetting").toString(), QStringLiteral("4.75"));
    }

    // -------------------------------------------------------------
    // ai_advisor_invoke MCP envelope shape (issue #1054, tasks.md task 6)
    //
    // The MCP tool's success-path lambda in src/mcp/mcptools_ai.cpp builds
    // the envelope via:
    //     QJsonObject body{{"response", response}};
    //     const auto structured = AIManager::parseStructuredNext(response);
    //     if (structured.has_value())
    //         body.insert("structuredNext", *structured);
    //     finalize(body);
    // This test pins the omission semantics so a future refactor cannot
    // accidentally re-introduce a `null` placeholder.
    // -------------------------------------------------------------

    static QJsonObject buildMcpEnvelopeForResponse(const QString& response)
    {
        QJsonObject body{{"response", response}};
        const auto structured = AIManager::parseStructuredNext(response);
        if (structured.has_value()) {
            body.insert(QStringLiteral("structuredNext"), *structured);
        }
        return body;
    }

    void aiAdvisorInvokeSurfacesStructuredNextOnRecommendation()
    {
        const QString reply = QStringLiteral(
            "Try grinder 4.75.\n\n```json\n{"
            "\"grinderSetting\":\"4.75\","
            "\"expectedDurationSec\":[32,38],"
            "\"expectedFlowMlPerSec\":[1.0,1.5],"
            "\"successCondition\":\"OK\","
            "\"reasoning\":\"slow flow toward profile target\"}\n```");
        const QJsonObject env = buildMcpEnvelopeForResponse(reply);
        QVERIFY2(env.contains("structuredNext"),
                 "ai_advisor_invoke envelope must surface structuredNext on a recommendation reply");
        const QJsonObject sn = env.value("structuredNext").toObject();
        QCOMPARE(sn.value("grinderSetting").toString(), QStringLiteral("4.75"));
        QCOMPARE(sn.value("expectedDurationSec").toArray().size(), 2);
        QCOMPARE(env.value("response").toString(), reply);  // prose unchanged
    }

    void aiAdvisorInvokeOmitsStructuredNextOnClarifyingResponse()
    {
        const QString reply = QStringLiteral(
            "How did this shot taste? Please give a 1-100 score and 1-2 lines of notes.");
        const QJsonObject env = buildMcpEnvelopeForResponse(reply);
        QVERIFY2(!env.contains("structuredNext"),
                 "absent structuredNext must be omitted, not emitted as null placeholder");
        // Defensive: scan the serialized envelope to be sure no null
        // placeholder slipped in via QJsonValue auto-conversion.
        const QByteArray serialized = QJsonDocument(env).toJson(QJsonDocument::Compact);
        QVERIFY2(!serialized.contains("structuredNext"),
                 "serialized envelope must not contain the structuredNext key when absent");
    }

    // -------------------------------------------------------------
    // AIConversation persistence of structuredNext (issue #1054)
    // -------------------------------------------------------------

    void aiConversation_addAssistantMessage_persistsStructuredNext()
    {
        AppSettings settings;
        settings.clear();

        QNetworkAccessManager nam;
        Settings appSettings;
        AIManager mgr(&nam, &appSettings);
        AIConversation conv(&mgr);
        conv.setStorageKey("test_structurednext_persist");

        const QString message = QStringLiteral(
            "Try grinder 4.75.\n\n```json\n{"
            "\"grinderSetting\":\"4.75\","
            "\"expectedDurationSec\":[32,38],"
            "\"expectedFlowMlPerSec\":[1.0,1.5],"
            "\"successCondition\":\"OK\","
            "\"reasoning\":\"r\"}\n```");

        // Direct persistence path: addUserMessage then addAssistantMessage
        // with a parsed structured block. We bypass the network round
        // trip so the test stays hermetic.
        const auto parsed = AIManager::parseStructuredNext(message);
        QVERIFY(parsed.has_value());

        // Friend access via tst_AIManager — see aiconversation.h DECENZA_TESTING block.
        conv.m_systemPrompt = QStringLiteral("system");
        conv.addUserMessage(QStringLiteral("user"));
        conv.addAssistantMessage(message, parsed);

        // Reader returns the parsed object on the latest assistant turn.
        const auto retrieved = conv.structuredNextForLastAssistantTurn();
        QVERIFY(retrieved.has_value());
        QCOMPARE(retrieved->value("grinderSetting").toString(), QStringLiteral("4.75"));

        // Saving + reloading round-trips the structured block.
        conv.saveToStorage();

        AIConversation conv2(&mgr);
        conv2.setStorageKey("test_structurednext_persist");
        conv2.loadFromStorage();
        const auto reloaded = conv2.structuredNextForLastAssistantTurn();
        QVERIFY2(reloaded.has_value(),
                 "structuredNext must round-trip through QSettings save/load");
        QCOMPARE(reloaded->value("grinderSetting").toString(), QStringLiteral("4.75"));
        QCOMPARE(reloaded->value("expectedDurationSec").toArray()[0].toInt(), 32);

        settings.clear();
    }

    void aiConversation_addAssistantMessage_omitsKeyWhenAbsent()
    {
        AppSettings settings;
        settings.clear();

        QNetworkAccessManager nam;
        Settings appSettings;
        AIManager mgr(&nam, &appSettings);
        AIConversation conv(&mgr);
        conv.setStorageKey("test_structurednext_absent");

        conv.m_systemPrompt = QStringLiteral("system");
        conv.addUserMessage(QStringLiteral("user"));
        conv.addAssistantMessage(QStringLiteral("clarifying question, no recommendation"));

        // No `structuredNext` key SHALL be written when the parser returns nullopt.
        QVERIFY(!conv.structuredNextForLastAssistantTurn().has_value());

        conv.saveToStorage();
        const QByteArray raw = AppSettings().value(
            QStringLiteral("ai/conversations/test_structurednext_absent/messages")).toByteArray();
        QVERIFY2(!raw.contains("structuredNext"),
                 "absent structuredNext must not be persisted as a key (no null placeholder)");

        settings.clear();
    }

    void aiConversation_loadsLegacyMessagesWithoutStructuredNext()
    {
        // A pre-#1054 saved conversation has assistant messages with
        // only {role, content}. Load must succeed; reader returns
        // nullopt for every assistant turn.
        AppSettings settings;
        settings.clear();
        const QString prefix = QStringLiteral("ai/conversations/test_structurednext_legacy/");
        settings.setValue(prefix + "systemPrompt", "system");
        const QByteArray legacyMessages = QByteArrayLiteral(
            "[{\"role\":\"user\",\"content\":\"u\"},"
            "{\"role\":\"assistant\",\"content\":\"a\"}]");
        settings.setValue(prefix + "messages", legacyMessages);

        QNetworkAccessManager nam;
        Settings appSettings;
        AIManager mgr(&nam, &appSettings);
        AIConversation conv(&mgr);
        conv.setStorageKey("test_structurednext_legacy");
        conv.loadFromStorage();

        QVERIFY2(!conv.structuredNextForLastAssistantTurn().has_value(),
                 "legacy assistant turns must read as no-structuredNext, not as malformed");

        settings.clear();
    }

    // fix-multishot-advice-tracking manual verification: a real, on-screen
    // in-app response never made it into persisted storage. Root cause:
    // saveToStorage() did a blind full-array overwrite from m_messages,
    // discarding any turn appendAssistantTurnForKey (the MCP ai_advisor_invoke
    // path) had written to the same key in the meantime. Reproduces the race
    // directly and asserts saveToStorage() now reconciles instead of clobbering.
    void aiConversation_saveToStorage_reconcilesTurnsAppendedByAnotherWriter()
    {
        AppSettings settings;
        settings.clear();

        QNetworkAccessManager nam;
        Settings appSettings;
        AIManager mgr(&nam, &appSettings);
        AIConversation conv(&mgr);
        conv.setStorageKey("test_save_race");
        conv.m_systemPrompt = QStringLiteral("system");

        conv.addUserMessage(QStringLiteral("u1"));
        conv.addAssistantMessage(QStringLiteral("a1"));
        conv.saveToStorage();
        QVERIFY2(conv.m_unsyncedMessages.isEmpty(), "saveToStorage must clear the pending-unsynced queue");

        // Another writer (simulating ai_advisor_invoke) appends a turn to the
        // SAME key, bypassing conv's in-memory state entirely — exactly what
        // appendAssistantTurnForKey does in production.
        AIConversation::appendAssistantTurnForKey(
            QStringLiteral("test_save_race"), 999,
            QStringLiteral("external user"), QStringLiteral("external assistant"), std::nullopt);

        // conv is unaware of the external turn — its own in-memory state is
        // still just [u1, a1] when it adds a further turn of its own.
        conv.addUserMessage(QStringLiteral("u2"));
        conv.addAssistantMessage(QStringLiteral("a2"));
        conv.saveToStorage();

        AIConversation conv2(&mgr);
        conv2.setStorageKey("test_save_race");
        conv2.loadFromStorage();

        QCOMPARE(conv2.messageCount(), 6);
        const QString text = conv2.getConversationText();
        QVERIFY2(text.contains("external user"),
                 "the externally-appended turn must survive conv's later save, not be clobbered");
        QVERIFY2(text.contains("u2"),
                 "conv's own new turn must also survive the reconciliation");
        QCOMPARE(conv2.shotIdForTurn(2), qint64(999));  // external user turn retains its shotId

        settings.clear();
    }

    // Root-cause regression: a conversation object that never called
    // loadFromStorage() at all (m_unsyncedMessages empty for the "never
    // synced" reason, not the "intentionally discarded" reason) must still
    // reconcile rather than blindly overwrite. This is the exact shape of
    // the bug found in manual verification — AIManager::switchConversation
    // used to skip loadFromStorage() for a key not yet in its own in-app
    // index, even though the MCP path had already written real turns there.
    void aiConversation_saveToStorage_reconcilesEvenWhenNeverLoaded()
    {
        AppSettings settings;
        settings.clear();

        // AIManager's ctor runs a one-time clearAllConversationsOnce
        // migration that wipes the whole ai/conversations group — must
        // construct it BEFORE writing the "external" data below, or the
        // migration deletes what we're about to write (see
        // mcpAiConversationGet_orphanedKey_fallsBackToStoredTimestamp for
        // the same gotcha).
        QNetworkAccessManager nam;
        Settings appSettings;
        AIManager mgr(&nam, &appSettings);

        // Real content already on disk, written entirely by "another writer"
        // (simulating ai_advisor_invoke) before this AIConversation object
        // ever touches the key.
        AIConversation::appendAssistantTurnForKey(
            QStringLiteral("test_never_loaded"), 111,
            QStringLiteral("mcp user"), QStringLiteral("mcp assistant"), std::nullopt);

        AIConversation conv(&mgr);
        conv.setStorageKey("test_never_loaded");
        conv.m_systemPrompt = QStringLiteral("system");
        // Deliberately no loadFromStorage() call — conv has no idea the key
        // already has 2 messages on disk, exactly like a freshly-constructed
        // conversation switched to via the pre-fix switchConversation().

        conv.addUserMessage(QStringLiteral("fresh user"));
        conv.addAssistantMessage(QStringLiteral("fresh assistant"));
        conv.saveToStorage();

        AIConversation conv2(&mgr);
        conv2.setStorageKey("test_never_loaded");
        conv2.loadFromStorage();

        QCOMPARE(conv2.messageCount(), 4);
        const QString text = conv2.getConversationText();
        QVERIFY2(text.contains("mcp user"),
                 "pre-existing disk content must survive a save from an object that never loaded first");
        QVERIFY2(text.contains("fresh user"),
                 "conv's own new turn must also be present");

        settings.clear();
    }

    // Root-cause fix: AIManager::switchConversation must load real disk
    // content for a key even when that key was never added to
    // m_conversationIndex (i.e. only ever written by the MCP
    // ai_advisor_invoke path's AIConversation::appendAssistantTurnForKey,
    // which doesn't touch the index). Before this fix, hasHistory() read
    // false for such a key and the in-app flow would call ask() — silently
    // discarding the real turns on the next save.
    void switchConversation_loadsRealDiskContentNotInIndex()
    {
        AppSettings settings;
        settings.clear();

        QNetworkAccessManager nam;
        Settings appSettings;
        AIManager mgr(&nam, &appSettings);

        ShotProjection keyShot;
        keyShot.beanBrand = QStringLiteral("Rogue Wave");
        keyShot.beanType = QStringLiteral("Ethiopia Yirgacheffe");
        keyShot.profileName = QStringLiteral("D-Flow");
        const QString key = AIManager::conversationKey(keyShot);
        // Written entirely by "another writer" — never touches m_conversationIndex.
        AIConversation::appendAssistantTurnForKey(
            key, 222, QStringLiteral("mcp-only user"), QStringLiteral("mcp-only assistant"), std::nullopt);

        // This AIManager's index has never heard of this key.
        mgr.switchConversation(QVariant::fromValue(keyShot));

        QVERIFY2(mgr.conversation()->hasHistory(),
                 "switchConversation must load real disk content even for a key absent from m_conversationIndex");
        QCOMPARE(mgr.conversation()->messageCount(), 2);

        settings.clear();
    }

    // -------------------------------------------------------------
    // Per-turn shot linkage on AIConversation (issue #1053 Part A)
    // -------------------------------------------------------------

    void aiConversation_setShotIdForCurrentTurn_appliesToUserAndAssistantOfPair()
    {
        QNetworkAccessManager nam;
        Settings appSettings;
        AIManager mgr(&nam, &appSettings);
        AIConversation conv(&mgr);
        conv.m_systemPrompt = "system";

        conv.addUserMessage("first user");
        conv.addAssistantMessage("first assistant");  // no shotId — legacy turn

        conv.addUserMessage("second user");
        conv.setShotIdForCurrentTurn(8473);
        conv.addAssistantMessage("second assistant", AIManager::parseStructuredNext(
            QStringLiteral("a\n```json\n{\"grinderSetting\":\"4.75\","
                "\"expectedDurationSec\":[32,38],"
                "\"expectedFlowMlPerSec\":[1.0,1.5],"
                "\"successCondition\":\"OK\","
                "\"reasoning\":\"r\"}\n```")));

        QCOMPARE(conv.shotIdForTurn(0), 0);  // legacy turn
        QCOMPARE(conv.shotIdForTurn(1), 0);
        QCOMPARE(conv.shotIdForTurn(2), 8473);  // user turn of pair 2
        QCOMPARE(conv.shotIdForTurn(3), 8473);  // assistant turn of pair 2
    }

    // fix-multishot-advice-tracking, task 5.1: pins the exact sequence
    // ConversationOverlay.qml's sendFollowUp() performs — stamp shotId
    // BEFORE the turn is sent, guarded on shotId > 0 — for both the
    // ask() (new conversation) and followUp() (existing conversation)
    // branches sendFollowUp() dispatches to. ask()/followUp() themselves
    // require a live provider (network), so this mirrors their internal
    // effect via addUserMessage/addAssistantMessage, exactly like the
    // test above.
    void sendFollowUpEquivalent_stampsShotIdBeforeAskAndFollowUp()
    {
        QNetworkAccessManager nam;
        Settings appSettings;
        AIManager mgr(&nam, &appSettings);
        AIConversation conv(&mgr);
        conv.m_systemPrompt = "system";

        // ask() branch: new conversation, overlay.shotId resolved (>0).
        const qint64 overlayShotId1 = 555;
        if (overlayShotId1 > 0)
            conv.setShotIdForCurrentTurn(overlayShotId1);
        conv.addUserMessage("first message");
        conv.addAssistantMessage("first reply");
        QCOMPARE(conv.shotIdForTurn(0), overlayShotId1);
        QCOMPARE(conv.shotIdForTurn(1), overlayShotId1);

        // followUp() branch: existing conversation, a later shot resolved.
        const qint64 overlayShotId2 = 556;
        if (overlayShotId2 > 0)
            conv.setShotIdForCurrentTurn(overlayShotId2);
        conv.addUserMessage("second message");
        conv.addAssistantMessage("second reply");
        QCOMPARE(conv.shotIdForTurn(2), overlayShotId2);
        QCOMPARE(conv.shotIdForTurn(3), overlayShotId2);
    }

    // The QML guard (`if (overlay.shotId > 0)`) must skip the stamp for a
    // free-form follow-up with no resolved shot — a stale/wrong id must
    // NOT get attached, matching the guard's purpose in
    // ConversationOverlay.qml's sendFollowUp().
    void sendFollowUpEquivalent_unresolvedShotIdSkipsStamp()
    {
        QNetworkAccessManager nam;
        Settings appSettings;
        AIManager mgr(&nam, &appSettings);
        AIConversation conv(&mgr);
        conv.m_systemPrompt = "system";

        const qint64 overlayShotId = 0;  // no resolved shot
        if (overlayShotId > 0)
            conv.setShotIdForCurrentTurn(overlayShotId);
        conv.addUserMessage("general question");
        conv.addAssistantMessage("general reply");

        QCOMPARE(conv.shotIdForTurn(0), qint64(0));
        QCOMPARE(conv.shotIdForTurn(1), qint64(0));
    }

    void aiConversation_recentAssistantTurns_skipsLegacyAndQuestionTurns()
    {
        QNetworkAccessManager nam;
        Settings appSettings;
        AIManager mgr(&nam, &appSettings);
        AIConversation conv(&mgr);
        conv.m_systemPrompt = "system";

        // Turn 0: legacy (no shotId, no structuredNext) — must skip.
        conv.addUserMessage("u0");
        conv.addAssistantMessage("a0");

        // Turn 1: shotId set but no structuredNext (clarifying-question
        // response) — must skip.
        conv.addUserMessage("u1");
        conv.setShotIdForCurrentTurn(100);
        conv.addAssistantMessage("How did this taste?");

        // Turn 2: shotId + structuredNext — qualifies.
        conv.addUserMessage("u2");
        conv.setShotIdForCurrentTurn(101);
        conv.addAssistantMessage(
            QStringLiteral("Try grinder 4.75.\n\n```json\n{\"grinderSetting\":\"4.75\","
                "\"expectedDurationSec\":[32,38],"
                "\"expectedFlowMlPerSec\":[1.0,1.5],"
                "\"successCondition\":\"OK\","
                "\"reasoning\":\"slow flow\"}\n```"),
            AIManager::parseStructuredNext(
                QStringLiteral("...\n```json\n{\"grinderSetting\":\"4.75\","
                    "\"expectedDurationSec\":[32,38],"
                    "\"expectedFlowMlPerSec\":[1.0,1.5],"
                    "\"successCondition\":\"OK\","
                    "\"reasoning\":\"slow flow\"}\n```")));

        const auto turns = conv.recentAssistantTurns(5);
        QCOMPARE(turns.size(), 1);
        QCOMPARE(turns.first().shotId, qint64(101));
        QCOMPARE(turns.first().structuredNext.value("grinderSetting").toString(),
                 QStringLiteral("4.75"));
    }

    void aiConversation_recentAssistantTurns_capsAtMax()
    {
        QNetworkAccessManager nam;
        Settings appSettings;
        AIManager mgr(&nam, &appSettings);
        AIConversation conv(&mgr);
        conv.m_systemPrompt = "system";

        const auto sn = AIManager::parseStructuredNext(
            QStringLiteral("a\n```json\n{\"grinderSetting\":\"4.75\","
                "\"expectedDurationSec\":[32,38],"
                "\"expectedFlowMlPerSec\":[1.0,1.5],"
                "\"successCondition\":\"OK\","
                "\"reasoning\":\"r\"}\n```"));
        QVERIFY(sn.has_value());

        // 5 qualifying turns; ask for at most 3.
        for (int i = 0; i < 5; ++i) {
            conv.addUserMessage(QString("u%1").arg(i));
            conv.setShotIdForCurrentTurn(100 + i);
            conv.addAssistantMessage(QString("a%1").arg(i), sn);
        }

        const auto turns = conv.recentAssistantTurns(3);
        QCOMPARE(turns.size(), 3);
        // Most-recent-first: shotId 104, 103, 102.
        QCOMPARE(turns.at(0).shotId, qint64(104));
        QCOMPARE(turns.at(1).shotId, qint64(103));
        QCOMPARE(turns.at(2).shotId, qint64(102));
    }

    void aiConversation_loadRecentAssistantTurnsForKey_static()
    {
        // The static loader is the parity path used by ai_advisor_invoke.
        // Round-trip: write a conversation via QSettings directly, then
        // assert the static loader returns the qualifying turns.
        AppSettings s;
        s.clear();
        const QString key = "test_recent_advice_static";
        const QString prefix = QStringLiteral("ai/conversations/") + key + "/";
        const QByteArray messages = QByteArrayLiteral(
            "[{\"role\":\"user\",\"content\":\"u0\",\"shotId\":100},"
            "{\"role\":\"assistant\",\"content\":\"a0\",\"shotId\":100,"
                "\"structuredNext\":{\"grinderSetting\":\"4.75\","
                    "\"expectedDurationSec\":[32,38],"
                    "\"expectedFlowMlPerSec\":[1.0,1.5],"
                    "\"successCondition\":\"OK\","
                    "\"reasoning\":\"r\"}},"
            "{\"role\":\"user\",\"content\":\"u1\"},"
            "{\"role\":\"assistant\",\"content\":\"a1\"}]");
        s.setValue(prefix + "messages", messages);

        const auto turns = AIConversation::loadRecentAssistantTurnsForKey(key, 3);
        QCOMPARE(turns.size(), 1);  // turn 1 has no shotId / no structuredNext
        QCOMPARE(turns.first().shotId, qint64(100));
        QCOMPARE(turns.first().structuredNext.value("grinderSetting").toString(),
                 QStringLiteral("4.75"));

        s.clear();
    }

    void aiConversation_recentAdviceParity_inAppMatchesMcpStaticLoader()
    {
        // Spec scenario: "Parity between in-app advisor and ai_advisor_invoke".
        // The in-app surface reads recent assistant turns via the live
        // AIConversation; the MCP surface reads them via the static
        // loadRecentAssistantTurnsForKey. Both must return byte-equivalent
        // turn lists for the same persisted conversation. Without parity,
        // the recentAdvice block built by buildRecentAdviceBlock cannot be
        // byte-equivalent across surfaces (#1041 parity contract).
        AppSettings s;
        s.clear();

        // Create AIManager first so clearAllConversationsOnce() fires on empty
        // settings and marks itself done — otherwise it would wipe the test
        // data we store below (the marker lives in QSettings and is absent
        // after s.clear(), causing the migration to re-fire on every CI run).
        QNetworkAccessManager nam;
        Settings appSettings;
        AIManager mgr(&nam, &appSettings);

        const QString key = "test_recent_advice_parity";
        const QString prefix = QStringLiteral("ai/conversations/") + key + "/";

        // Three assistant turns: one with shotId only, one with structuredNext
        // only, one with both. Only the latter should appear in either path's
        // returned list — test pins that filter consistency too.
        const QByteArray messages = QByteArrayLiteral(
            "[{\"role\":\"user\",\"content\":\"u0\",\"shotId\":100},"
            "{\"role\":\"assistant\",\"content\":\"a0\",\"shotId\":100},"
            "{\"role\":\"user\",\"content\":\"u1\"},"
            "{\"role\":\"assistant\",\"content\":\"a1\","
                "\"structuredNext\":{\"grinderSetting\":\"4.75\","
                "\"expectedDurationSec\":[32,38],"
                "\"expectedFlowMlPerSec\":[1.0,1.5],"
                "\"successCondition\":\"OK\","
                "\"reasoning\":\"r\"}},"
            "{\"role\":\"user\",\"content\":\"u2\",\"shotId\":102},"
            "{\"role\":\"assistant\",\"content\":\"a2\",\"shotId\":102,"
                "\"structuredNext\":{\"grinderSetting\":\"4.5\","
                "\"expectedDurationSec\":[30,36],"
                "\"expectedFlowMlPerSec\":[1.1,1.6],"
                "\"successCondition\":\"OK\","
                "\"reasoning\":\"r2\"}}]");
        s.setValue(prefix + "systemPrompt", "system");
        s.setValue(prefix + "messages", messages);
        AIConversation conv(&mgr);
        conv.setStorageKey(key);
        conv.loadFromStorage();
        const auto inApp = conv.recentAssistantTurns(3);

        // MCP: static loader over the same QSettings layout.
        const auto mcp = AIConversation::loadRecentAssistantTurnsForKey(key, 3);

        QCOMPARE(inApp.size(), mcp.size());
        QCOMPARE(inApp.size(), qsizetype(1));  // only the qualifying turn
        QCOMPARE(inApp.first().shotId, qint64(102));
        QCOMPARE(inApp.first().shotId, mcp.first().shotId);
        QCOMPARE(inApp.first().structuredNext, mcp.first().structuredNext);
        QCOMPARE(inApp.first().content, mcp.first().content);

        s.clear();
    }

    // -------------------------------------------------------------
    // Layer 1: parseUserRatingReply (issue #1055)
    // -------------------------------------------------------------

    void parseUserRatingReply_extractsBareNumber()
    {
        const auto parsed = AIManager::parseUserRatingReply(QStringLiteral("82"));
        QVERIFY(parsed.has_value());
        QCOMPARE(parsed->score, 82);
        QVERIFY(parsed->notes.isEmpty());
    }

    void parseUserRatingReply_extractsNumberWithNotes()
    {
        const auto parsed = AIManager::parseUserRatingReply(QStringLiteral("82, balanced and sweet"));
        QVERIFY(parsed.has_value());
        QCOMPARE(parsed->score, 82);
        QCOMPARE(parsed->notes, QStringLiteral("balanced and sweet"));
    }

    void parseUserRatingReply_acceptsOutOf100()
    {
        const auto parsed = AIManager::parseUserRatingReply(QStringLiteral("75 out of 100"));
        QVERIFY(parsed.has_value());
        QCOMPARE(parsed->score, 75);
    }

    void parseUserRatingReply_acceptsSlash100AndPercent()
    {
        const auto a = AIManager::parseUserRatingReply(QStringLiteral("70/100"));
        QVERIFY(a.has_value()); QCOMPARE(a->score, 70);
        const auto b = AIManager::parseUserRatingReply(QStringLiteral("65%"));
        QVERIFY(b.has_value()); QCOMPARE(b->score, 65);
    }

    void parseUserRatingReply_decimalsRoundToNearest()
    {
        const auto parsed = AIManager::parseUserRatingReply(QStringLiteral("82.5"));
        QVERIFY(parsed.has_value());
        QCOMPARE(parsed->score, 83);
    }

    void parseUserRatingReply_rejectsNonNumeric()
    {
        QVERIFY(!AIManager::parseUserRatingReply(
            QStringLiteral("really good, much better than last time")).has_value());
        QVERIFY(!AIManager::parseUserRatingReply(QStringLiteral("loved it")).has_value());
    }

    void parseUserRatingReply_rejectsOutOfRange()
    {
        QVERIFY(!AIManager::parseUserRatingReply(QStringLiteral("0")).has_value());
        QVERIFY(!AIManager::parseUserRatingReply(QStringLiteral("150")).has_value());
        QVERIFY(!AIManager::parseUserRatingReply(QStringLiteral("-5")).has_value());
    }

    void parseUserRatingReply_leadingTokenWinsOverLaterTokens()
    {
        // Leading-token rule: only the first non-whitespace token (or
        // a suffixed number anywhere) qualifies. "80" leads → wins;
        // "85" appears later but no suffix and not leading → ignored.
        const auto parsed = AIManager::parseUserRatingReply(
            QStringLiteral("80, maybe 85 next time"));
        QVERIFY(parsed.has_value());
        QCOMPARE(parsed->score, 80);
        QVERIFY2(parsed->notes.contains(QStringLiteral("85")),
                 qPrintable("notes preserves the trailing context: " + parsed->notes));
    }

    void parseUserRatingReply_skipsOutOfRangeLeadingToken_rejectsMidProse()
    {
        // First token (200) is out-of-range; the in-range "75" later in
        // the sentence has no suffix and isn't leading, so the tighter
        // rule rejects it. The user wanted to score; the writeback
        // should bail and let them give a cleaner reply (e.g., "75/100").
        QVERIFY(!AIManager::parseUserRatingReply(
            QStringLiteral("compared to my 200g batch, this was a 75")).has_value());
    }

    void parseUserRatingReply_emptyInput()
    {
        QVERIFY(!AIManager::parseUserRatingReply(QString()).has_value());
        QVERIFY(!AIManager::parseUserRatingReply(QStringLiteral("   \n  ")).has_value());
    }

    void parseUserRatingReply_rejectsMidProseNumbersWithoutSuffix()
    {
        // A bare number deep in prose without a /100, out of 100, or %
        // suffix is NOT a score — earlier the loose regex extracted it
        // and misattributed grams / day-counts as ratings.
        QVERIFY2(!AIManager::parseUserRatingReply(
            QStringLiteral("I dosed 18 grams, pulled in 32 seconds")).has_value(),
            "must not pick up dose grams or duration as a score");
        QVERIFY2(!AIManager::parseUserRatingReply(
            QStringLiteral("Mid October roast, 30 days old")).has_value(),
            "must not pick up day counts as a score");
    }

    void parseUserRatingReply_acceptsSuffixedScoreInProse()
    {
        // When the user writes the suffixed form anywhere in the reply,
        // it's an unambiguous score and parses fine.
        const auto a = AIManager::parseUserRatingReply(
            QStringLiteral("compared to my 200g batch, this was 75 out of 100"));
        QVERIFY(a.has_value());
        QCOMPARE(a->score, 75);
    }

    // -------------------------------------------------------------
    // maybePersistRatingFromReply gating (issue #1055 Layer 1, tasks 3
    // items 8-10). The function fronts AIConversation::followUp's
    // conversational rating capture; we exercise the no-op short-circuits
    // here without spinning up a real ShotHistoryStorage. The "happy path
    // writes to DB" case requires a real m_shotHistory and is left to
    // integration testing — these tests pin the gating contract so a
    // future refactor cannot accidentally remove a guard.
    void maybePersistRatingFromReply_noopWhenShotIdZero()
    {
        QNetworkAccessManager nam;
        Settings appSettings;
        AIManager mgr(&nam, &appSettings);
        // m_shotHistory is null (we never set it). With shotId=0 the
        // function should short-circuit BEFORE reaching the m_shotHistory
        // dereference, so no crash + no warning.
        mgr.maybePersistRatingFromReply(
            QStringLiteral("82, balanced"),
            QStringLiteral("How did this taste? Please give a 1-100 score."),
            /*shotId=*/0);
        // Reaching this line without a crash IS the assertion.
        QVERIFY(true);
    }

    void maybePersistRatingFromReply_noopWhenShotHistoryUnset()
    {
        QNetworkAccessManager nam;
        Settings appSettings;
        AIManager mgr(&nam, &appSettings);
        // m_shotHistory is null. Even with a valid shotId + score, the
        // function should short-circuit cleanly.
        mgr.maybePersistRatingFromReply(
            QStringLiteral("82, balanced"),
            QStringLiteral("How did this taste? Please give a 1-100 score."),
            /*shotId=*/8473);
        QVERIFY(true);
    }

    void maybePersistRatingFromReply_noopWhenPriorDidntAskAboutTaste()
    {
        QNetworkAccessManager nam;
        Settings appSettings;
        AIManager mgr(&nam, &appSettings);
        // Even though the user reply has a numeric score and we have a
        // valid shotId, the prior assistant message does NOT match any
        // taste-question marker — the heuristic guard suppresses the
        // write so a stray number in unrelated conversation doesn't
        // get attached as a rating.
        mgr.maybePersistRatingFromReply(
            QStringLiteral("82"),
            QStringLiteral("Try a finer grind setting around 4.75."),
            /*shotId=*/8473);
        QVERIFY(true);
    }

    void maybePersistRatingFromReply_noopWhenReplyHasNoScore()
    {
        QNetworkAccessManager nam;
        Settings appSettings;
        AIManager mgr(&nam, &appSettings);
        // Prior asks about taste; user replies in prose with no score.
        // Parser returns nullopt; function short-circuits.
        mgr.maybePersistRatingFromReply(
            QStringLiteral("really good, much better than last time"),
            QStringLiteral("How did this taste?"),
            /*shotId=*/8473);
        QVERIFY(true);
    }

    // -------------------------------------------------------------
    // shot-metadata-capture: parseBeanCorrectionsFromReply
    // -------------------------------------------------------------

    void parseBeanCorrectionsFromReply_extractsRoastLevelExplicit()
    {
        const auto a = AIManager::parseBeanCorrectionsFromReply(
            QStringLiteral("actually it's really dark"));
        QVERIFY(a.has_value());
        QCOMPARE(a->roastLevel.value_or(QString()), QStringLiteral("Dark"));

        const auto b = AIManager::parseBeanCorrectionsFromReply(
            QStringLiteral("the coffee is medium-dark"));
        QVERIFY(b.has_value());
        QCOMPARE(b->roastLevel.value_or(QString()), QStringLiteral("Medium-Dark"));

        const auto c = AIManager::parseBeanCorrectionsFromReply(
            QStringLiteral("this is a light roast"));
        QVERIFY(c.has_value());
        QCOMPARE(c->roastLevel.value_or(QString()), QStringLiteral("Light"));
    }

    void parseBeanCorrectionsFromReply_canonicalizesRoastValues()
    {
        // "medium dark" / "medium-dark" / "MediumDark" all canonicalize to
        // the app's stored form "Medium-Dark".
        const auto a = AIManager::parseBeanCorrectionsFromReply(
            QStringLiteral("the coffee is medium dark"));
        QVERIFY(a.has_value());
        QCOMPARE(a->roastLevel.value_or(QString()), QStringLiteral("Medium-Dark"));

        const auto b = AIManager::parseBeanCorrectionsFromReply(
            QStringLiteral("the coffee is mediumdark"));
        QVERIFY(b.has_value());
        QCOMPARE(b->roastLevel.value_or(QString()), QStringLiteral("Medium-Dark"));

        const auto c = AIManager::parseBeanCorrectionsFromReply(
            QStringLiteral("the coffee is medium-light"));
        QVERIFY(c.has_value());
        QCOMPARE(c->roastLevel.value_or(QString()), QStringLiteral("Medium-Light"));
    }

    void parseBeanCorrectionsFromReply_rejectsCompoundPhrases()
    {
        // Compound phrases describing taste must NOT trigger a roast-level
        // correction. The parser requires a context word (coffee/bean/roast
        // /actually) to bind the adjective to roast level.
        const auto a = AIManager::parseBeanCorrectionsFromReply(
            QStringLiteral("dark chocolate notes, full body"));
        QVERIFY2(!a.has_value() || !a->roastLevel.has_value(),
                 "'dark chocolate' must not be parsed as roastLevel");

        const auto b = AIManager::parseBeanCorrectionsFromReply(
            QStringLiteral("light citrus and floral"));
        QVERIFY2(!b.has_value() || !b->roastLevel.has_value(),
                 "'light citrus' must not be parsed as roastLevel");

        const auto c = AIManager::parseBeanCorrectionsFromReply(
            QStringLiteral("medium body, balanced"));
        QVERIFY2(!c.has_value() || !c->roastLevel.has_value(),
                 "'medium body' must not be parsed as roastLevel");
    }

    void parseBeanCorrectionsFromReply_rejectsLooseBranchWithoutRoastSuffix()
    {
        // The "(this|it|that) is a (level)" branch is too broad without an
        // explicit "roast" suffix — "this is a dark crema" / "it's a light
        // body" describe the shot, not the bean's roast level. The parser
        // must require " roast" after the level word in this branch.
        const auto crema = AIManager::parseBeanCorrectionsFromReply(
            QStringLiteral("this is a dark crema, nice and thick"));
        QVERIFY2(!crema.has_value() || !crema->roastLevel.has_value(),
                 "'this is a dark crema' must not be parsed as roastLevel");

        const auto body = AIManager::parseBeanCorrectionsFromReply(
            QStringLiteral("it's a light body shot"));
        QVERIFY2(!body.has_value() || !body->roastLevel.has_value(),
                 "'it's a light body' must not be parsed as roastLevel");

        // But "this is a dark roast" (with the suffix) IS a roast-level
        // correction and must still match.
        const auto roast = AIManager::parseBeanCorrectionsFromReply(
            QStringLiteral("this is a dark roast"));
        QVERIFY(roast.has_value());
        QCOMPARE(roast->roastLevel.value_or(QString()), QStringLiteral("Dark"));
    }

    void parseBeanCorrectionsFromReply_extractsBeanBrand()
    {
        const auto a = AIManager::parseBeanCorrectionsFromReply(
            QStringLiteral("actually it's from Sey"));
        QVERIFY(a.has_value());
        QCOMPARE(a->beanBrand.value_or(QString()), QStringLiteral("Sey"));

        const auto b = AIManager::parseBeanCorrectionsFromReply(
            QStringLiteral("the roaster is Onyx Coffee Lab"));
        QVERIFY(b.has_value());
        QCOMPARE(b->beanBrand.value_or(QString()), QStringLiteral("Onyx Coffee Lab"));
    }

    void parseBeanCorrectionsFromReply_rejectsBrandFromProseAfterLead()
    {
        // The brand capture is bounded to 1-4 word tokens AND must begin
        // with an uppercase letter, so prose replies after a recognised
        // lead-in ("the roaster is having problems with the new burr today")
        // do NOT produce a bogus brand write. The lowercase first word
        // "having" is the discriminator here.
        const auto a = AIManager::parseBeanCorrectionsFromReply(
            QStringLiteral("the roaster is having problems with the new burr today"));
        QVERIFY2(!a.has_value() || !a->beanBrand.has_value(),
                 "lowercase prose continuation must not be captured as a brand");

        const auto b = AIManager::parseBeanCorrectionsFromReply(
            QStringLiteral("actually it's from somewhere with the new burr today"));
        QVERIFY2(!b.has_value() || !b->beanBrand.has_value(),
                 "lowercase 'somewhere' must not be captured as a brand");
    }

    void parseBeanCorrectionsFromReply_extractsRoastDateIso()
    {
        const auto a = AIManager::parseBeanCorrectionsFromReply(
            QStringLiteral("roasted 2026-04-15"));
        QVERIFY(a.has_value());
        QCOMPARE(a->roastDate.value_or(QString()), QStringLiteral("2026-04-15"));

        const auto b = AIManager::parseBeanCorrectionsFromReply(
            QStringLiteral("roasted on 2026-03-21, very fresh"));
        QVERIFY(b.has_value());
        QCOMPARE(b->roastDate.value_or(QString()), QStringLiteral("2026-03-21"));
    }

    void parseBeanCorrectionsFromReply_extractsRoastDateNatural()
    {
        const auto a = AIManager::parseBeanCorrectionsFromReply(
            QStringLiteral("roasted April 15, 2026"));
        QVERIFY(a.has_value());
        QCOMPARE(a->roastDate.value_or(QString()), QStringLiteral("2026-04-15"));

        const auto b = AIManager::parseBeanCorrectionsFromReply(
            QStringLiteral("roasted on March 5"));
        QVERIFY(b.has_value());
        const int year = QDate::currentDate().year();
        QCOMPARE(b->roastDate.value_or(QString()),
                 QDate(year, 3, 5).toString(QStringLiteral("yyyy-MM-dd")));
    }

    void parseBeanCorrectionsFromReply_handlesMultipleFields()
    {
        // A single reply may carry multiple corrections — both should be set.
        const auto a = AIManager::parseBeanCorrectionsFromReply(
            QStringLiteral("actually it's from Sey, the coffee is dark"));
        QVERIFY(a.has_value());
        QCOMPARE(a->beanBrand.value_or(QString()), QStringLiteral("Sey"));
        QCOMPARE(a->roastLevel.value_or(QString()), QStringLiteral("Dark"));
    }

    void parseBeanCorrectionsFromReply_emptyOnUnrelated()
    {
        QVERIFY(!AIManager::parseBeanCorrectionsFromReply(
            QStringLiteral("really good shot, balanced and sweet")).has_value());
        QVERIFY(!AIManager::parseBeanCorrectionsFromReply(
            QStringLiteral("82")).has_value());
        QVERIFY(!AIManager::parseBeanCorrectionsFromReply(QString()).has_value());
        QVERIFY(!AIManager::parseBeanCorrectionsFromReply(QStringLiteral("   ")).has_value());
    }

    // -------------------------------------------------------------
    // shot-metadata-capture: maybePersistBeanCorrectionFromReply gating.
    // Mirrors the rating-capture short-circuit tests above. The "happy
    // path writes to DB" case requires a real m_shotHistory and is left
    // to integration testing — these pin the gates.
    // -------------------------------------------------------------

    void maybePersistBeanCorrectionFromReply_noopWhenShotIdZero()
    {
        QNetworkAccessManager nam;
        Settings appSettings;
        AIManager mgr(&nam, &appSettings);
        mgr.maybePersistBeanCorrectionFromReply(
            QStringLiteral("actually it's really dark"),
            QStringLiteral("What roast level is this?"),
            /*shotId=*/0);
        QVERIFY(true);
    }

    void maybePersistBeanCorrectionFromReply_noopWhenShotHistoryUnset()
    {
        QNetworkAccessManager nam;
        Settings appSettings;
        AIManager mgr(&nam, &appSettings);
        mgr.maybePersistBeanCorrectionFromReply(
            QStringLiteral("actually it's really dark"),
            QStringLiteral("What roast level is this?"),
            /*shotId=*/8473);
        QVERIFY(true);
    }

    void maybePersistBeanCorrectionFromReply_noopWhenBothGatesFail()
    {
        QNetworkAccessManager nam;
        Settings appSettings;
        AIManager mgr(&nam, &appSettings);
        // Parser matches ("roast level is dark" → Dark) BUT the prior
        // assistant message didn't ask about beans AND the user reply
        // doesn't contain any volunteer marker ("roast level is" is in
        // the parser but not in the volunteers list — the volunteers
        // list requires explicit corrective phrasings like "the coffee
        // is" or "actually..."). Both gates closed → no write.
        mgr.maybePersistBeanCorrectionFromReply(
            QStringLiteral("roast level is dark"),
            QStringLiteral("Try a finer grind setting around 4.75."),
            /*shotId=*/8473);
        QVERIFY(true);
    }

    void maybePersistBeanCorrectionFromReply_noopWhenParserEmpty()
    {
        QNetworkAccessManager nam;
        Settings appSettings;
        AIManager mgr(&nam, &appSettings);
        // Reply has corrective phrasing markers but no recognisable
        // bean-field correction. Parser returns nullopt; function short-circuits.
        mgr.maybePersistBeanCorrectionFromReply(
            QStringLiteral("actually it's complicated, maybe try again"),
            QStringLiteral("What roast level is this?"),
            /*shotId=*/8473);
        QVERIFY(true);
    }

    // -------------------------------------------------------------
    // Static appendAssistantTurnForKey (#1055 follow-up: MCP
    // ai_advisor_invoke write-through). Lets surfaces persist a
    // user/assistant pair into the conversation at `storageKey`
    // without going through a live AIConversation. Subsequent
    // recentAdvice loads see the turn.
    // -------------------------------------------------------------

    void appendAssistantTurnForKey_writesUserAndAssistantWithShotId()
    {
        AppSettings s;
        s.clear();
        const QString key = "test_append_static";

        QJsonObject sn{
            {"grinderSetting", "4.75"},
            {"expectedDurationSec", QJsonArray{32, 38}},
            {"expectedFlowMlPerSec", QJsonArray{1.0, 1.5}},
            {"successCondition", "OK"},
            {"reasoning", "slow flow toward target"}
        };

        AIConversation::appendAssistantTurnForKey(
            key, /*shotId=*/8473,
            QStringLiteral("user prompt content"),
            QStringLiteral("Try grinder 4.75."),
            sn);

        // Verify via the static loader that the assistant turn qualifies.
        const auto turns = AIConversation::loadRecentAssistantTurnsForKey(key, 3);
        QCOMPARE(turns.size(), 1);
        QCOMPARE(turns.first().shotId, qint64(8473));
        QCOMPARE(turns.first().structuredNext.value("grinderSetting").toString(),
                 QStringLiteral("4.75"));

        // Verify the persisted bytes contain a user message with shotId.
        const QByteArray raw = AppSettings().value(
            QStringLiteral("ai/conversations/") + key + QStringLiteral("/messages"))
            .toByteArray();
        const QJsonArray arr = QJsonDocument::fromJson(raw).array();
        QCOMPARE(arr.size(), 2);
        QCOMPARE(arr.at(0).toObject().value("role").toString(), QStringLiteral("user"));
        QCOMPARE(static_cast<qint64>(arr.at(0).toObject().value("shotId").toDouble()),
                 qint64(8473));
        QCOMPARE(arr.at(1).toObject().value("role").toString(), QStringLiteral("assistant"));

        s.clear();
    }

    void appendAssistantTurnForKey_appendsRatherThanOverwrites()
    {
        AppSettings s;
        s.clear();
        const QString key = "test_append_grows";

        const QJsonObject sn{
            {"grinderSetting", "4.75"},
            {"expectedDurationSec", QJsonArray{32, 38}},
            {"expectedFlowMlPerSec", QJsonArray{1.0, 1.5}},
            {"successCondition", "OK"},
            {"reasoning", "r"}
        };

        AIConversation::appendAssistantTurnForKey(
            key, 100, "u1", "a1", sn);
        AIConversation::appendAssistantTurnForKey(
            key, 105, "u2", "a2", sn);

        // Two pairs => 4 messages.
        const QByteArray raw = AppSettings().value(
            QStringLiteral("ai/conversations/") + key + QStringLiteral("/messages"))
            .toByteArray();
        const QJsonArray arr = QJsonDocument::fromJson(raw).array();
        QCOMPARE(arr.size(), 4);

        // Static loader returns most-recent-first, capped.
        const auto turns = AIConversation::loadRecentAssistantTurnsForKey(key, 3);
        QCOMPARE(turns.size(), 2);
        QCOMPARE(turns.at(0).shotId, qint64(105));
        QCOMPARE(turns.at(1).shotId, qint64(100));

        s.clear();
    }

    void appendAssistantTurnForKey_omitsStructuredNextWhenAbsent()
    {
        AppSettings s;
        s.clear();
        const QString key = "test_append_no_sn";

        AIConversation::appendAssistantTurnForKey(
            key, 200, "u", "clarifying question, no rec",
            std::nullopt);

        const QByteArray raw = AppSettings().value(
            QStringLiteral("ai/conversations/") + key + QStringLiteral("/messages"))
            .toByteArray();
        QVERIFY2(!raw.contains("structuredNext"),
                 "absent structuredNext must not be persisted as a key");

        // Loader skips assistant turns missing structuredNext, so this
        // turn does not qualify for recentAdvice — exactly the MCP
        // behaviour for a clarifying-question reply.
        const auto turns = AIConversation::loadRecentAssistantTurnsForKey(key, 3);
        QVERIFY(turns.isEmpty());

        s.clear();
    }

    void appendAssistantTurnForKey_emptyKeyIsNoOp()
    {
        AppSettings s;
        s.clear();
        AIConversation::appendAssistantTurnForKey(
            QString(), 100, "u", "a", std::nullopt);
        // No assertion needed: this just must not crash and must not
        // create any settings keys.
        QVERIFY(true);
        s.clear();
    }

    void aiConversation_setShotIdForCurrentTurn_legacyConversationHasZeroShotId()
    {
        // A pre-#1053 conversation has no shotId on any entry; reader
        // must return 0 without error.
        AppSettings s;
        s.clear();

        // Construct AIManager first so clearAllConversationsOnce() fires on
        // empty settings and marks itself done before we write test data.
        QNetworkAccessManager nam;
        Settings appSettings;
        AIManager mgr(&nam, &appSettings);

        const QString key = "test_legacy_shotid";
        const QString prefix = QStringLiteral("ai/conversations/") + key + "/";
        s.setValue(prefix + "messages", QByteArrayLiteral(
            "[{\"role\":\"user\",\"content\":\"u\"},{\"role\":\"assistant\",\"content\":\"a\"}]"));
        AIConversation conv(&mgr);
        conv.setStorageKey(key);
        conv.loadFromStorage();

        QCOMPARE(conv.shotIdForTurn(0), 0);
        QCOMPARE(conv.shotIdForTurn(1), 0);
        QVERIFY(conv.recentAssistantTurns(3).isEmpty());

        s.clear();
    }

    // -----------------------------------------------------------------
    // AIConversation::stripStructuredNextBlock
    // Pins the fix from PR #1074: the trailing ```json ... ``` block the
    // AI appends must be stripped before display in getConversationText.
    // -----------------------------------------------------------------

    void stripStructuredNextBlock_noFencedBlock_returnsUnchanged()
    {
        const QString plain = QStringLiteral("Adjust your grinder to 4.5.");
        QCOMPARE(AIConversation::stripStructuredNextBlock(plain), plain);
    }

    void stripStructuredNextBlock_validTrailingJsonBlock_stripsBlock()
    {
        const QString content = QStringLiteral(
            "Try 4.75.\n\n```json\n{\"grinderSetting\":\"4.75\"}\n```");
        const QString result = AIConversation::stripStructuredNextBlock(content);
        QCOMPARE(result, QStringLiteral("Try 4.75."));
        QVERIFY2(!result.contains(QStringLiteral("```")),
                 "stripped result must not contain any fence markers");
    }

    void stripStructuredNextBlock_nonJsonTag_returnsUnchanged()
    {
        const QString content = QStringLiteral(
            "Try 4.75.\n\n```python\nprint('hello')\n```");
        QCOMPARE(AIConversation::stripStructuredNextBlock(content), content);
    }

    void stripStructuredNextBlock_trailingContentAfterCloser_returnsUnchanged()
    {
        const QString content = QStringLiteral(
            "Try 4.75.\n\n```json\n{\"grinderSetting\":\"4.75\"}\n```\nOne more thing.");
        QCOMPARE(AIConversation::stripStructuredNextBlock(content), content);
    }

    void stripStructuredNextBlock_oddFenceCount_stripsIfLastTwoFormValidBlock()
    {
        // Prose contains an earlier fenced block (even count before the json
        // block), then a valid trailing json block. The last two fences must
        // form the json block and be stripped; earlier fences are untouched.
        const QString content = QStringLiteral(
            "For reference:\n```plain\ncode\n```\n\n"
            "```json\n{\"grinderSetting\":\"4.75\"}\n```");
        const QString result = AIConversation::stripStructuredNextBlock(content);
        QVERIFY2(!result.contains(QStringLiteral("```json")),
                 "trailing json block must be stripped even when earlier fences exist");
        QVERIFY2(result.contains(QStringLiteral("For reference:")),
                 "prose before the json block must be preserved");
        QVERIFY2(result.contains(QStringLiteral("```plain")),
                 "earlier non-json fences must be preserved");
    }

    void stripStructuredNextBlock_missingNewlineAfterTag_returnsUnchanged()
    {
        // No newline between the opening tag and the JSON body — the tag
        // check requires a newline delimiter; without it the block is
        // malformed and must not strip.
        const QString content = QStringLiteral(
            "Try 4.75.\n\n```json{\"grinderSetting\":\"4.75\"}\n```");
        QCOMPARE(AIConversation::stripStructuredNextBlock(content), content);
    }

    void aiConversation_getConversationText_stripsJsonBlock()
    {
        AppSettings s;
        s.clear();

        QNetworkAccessManager nam;
        Settings appSettings;
        AIManager mgr(&nam, &appSettings);
        AIConversation conv(&mgr);
        conv.setStorageKey(QStringLiteral("test_strip_conversation_text"));

        conv.m_systemPrompt = QStringLiteral("system");
        conv.addUserMessage(QStringLiteral("What grind setting?"));

        const QString response = QStringLiteral(
            "Try grinder 4.75 for a 32-38 s shot.\n\n"
            "```json\n{\"grinderSetting\":\"4.75\","
            "\"expectedDurationSec\":[32,38],"
            "\"expectedFlowMlPerSec\":[1.0,1.5],"
            "\"successCondition\":\"OK\","
            "\"reasoning\":\"r\"}\n```");
        const auto parsed = AIManager::parseStructuredNext(response);
        conv.addAssistantMessage(response, parsed);

        const QString text = conv.getConversationText();

        QVERIFY2(text.contains(QStringLiteral("Try grinder 4.75")),
                 "prose advice must appear in conversation text");
        QVERIFY2(!text.contains(QStringLiteral("```json")),
                 "json fence must not appear in conversation text");
        QVERIFY2(!text.contains(QStringLiteral("grinderSetting")),
                 "json body must not appear in conversation text");

        s.clear();
    }

    // -------------------------------------------------------------
    // The documentation topics tool descriptions point at. A .md added to
    // resources/ai/tools/ but NOT to resources/ai.qrc is invisible at runtime — the
    // description still says get_agent_file topic "x" and the server reports it as
    // the caller's typo. This test links ai.qrc, so it is the one place that can
    // tell the difference between "shipped" and "in the tree".
    void agentDocTopicsAreShippedInTheResourceBundle()
    {
        const QStringList topics = McpAgentDocs::availableTopics();
        QVERIFY2(!topics.isEmpty(), "no :/ai/tools topics — ai.qrc did not link");
        for (const QString& topic : topics) {
            QFile doc(McpAgentDocs::topicPath(topic));
            QVERIFY2(doc.open(QIODevice::ReadOnly), qPrintable(topic));
            QVERIFY2(doc.size() > 0, qPrintable(topic));
        }
    }

    // The ai_conversations tool's list/get verbs (#639 support).
    // registerAIConversationTools lives in mcptools_ai_conversations.cpp —
    // linked into this target so it can run against a real AIManager.
    // -------------------------------------------------------------

    void mcpAiConversations_listAndGet_roundTripRealConversation()
    {
        AppSettings settings;
        settings.clear();

        QNetworkAccessManager nam;
        Settings appSettings;
        AIManager mgr(&nam, &appSettings);

        ShotProjection sw;
        sw.beanBrand = QStringLiteral("Rogue Wave");
        sw.beanType = QStringLiteral("Ethiopia Yirgacheffe");
        sw.profileName = QStringLiteral("D-Flow");
        const QString key = mgr.switchConversation(QVariant::fromValue(sw));
        AIConversation* conv = mgr.conversation();
        conv->m_systemPrompt = QStringLiteral("system prompt");
        conv->addUserMessage(QStringLiteral("Shot pulled at 19g/1:2"));
        const QString response = QStringLiteral("Try 4.75 on the grinder.");
        conv->addAssistantMessage(response);
        conv->saveToStorage();

        McpToolRegistry registry;
        registerAIConversationTools(&registry, &mgr);

        QString err;
        const QJsonObject listResult = callConversations(registry, {{"action", "list"}}, err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        const QJsonArray conversations = listResult["conversations"].toArray();
        QCOMPARE(conversations.size(), 1);
        const QJsonObject entry = conversations[0].toObject();
        QCOMPARE(entry["key"].toString(), key);
        QCOMPARE(entry["label"].toString(), QStringLiteral("Rogue Wave Ethiopia Yirgacheffe / D-Flow"));
        QCOMPARE(entry["messageCount"].toInt(), 2);
        QVERIFY(!entry["lastUpdated"].toString().isEmpty());
        QVERIFY2(!entry.contains("corrupted"), "healthy entries must not carry the corrupted key at all");

        const QJsonObject getResult = callConversations(registry, {{"action", "get"}, {"key", key}}, err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QVERIFY(!getResult.contains("error"));
        QCOMPARE(getResult["key"].toString(), key);
        QCOMPARE(getResult["systemPrompt"].toString(), QStringLiteral("system prompt"));
        const QJsonObject metadata = getResult["metadata"].toObject();
        QCOMPARE(metadata["beanBrand"].toString(), QStringLiteral("Rogue Wave"));
        QCOMPARE(metadata["profileName"].toString(), QStringLiteral("D-Flow"));
        QVERIFY(!metadata["lastUpdated"].toString().isEmpty());
        const QJsonArray messages = getResult["messages"].toArray();
        QCOMPARE(messages.size(), 2);
        QCOMPARE(messages[0].toObject()["role"].toString(), QStringLiteral("user"));
        QCOMPARE(messages[1].toObject()["role"].toString(), QStringLiteral("assistant"));
        QCOMPARE(messages[1].toObject()["content"].toString(), response);

        settings.clear();
    }

    void mcpAiConversationGet_missingKey_returnsErrorNotCrash()
    {
        AppSettings settings;
        settings.clear();

        QNetworkAccessManager nam;
        Settings appSettings;
        AIManager mgr(&nam, &appSettings);
        McpToolRegistry registry;
        registerAIConversationTools(&registry, &mgr);

        QString err;
        const QJsonObject r = callConversations(registry, {{"action", "get"}, {"key", "nonexistent_key"}}, err);
        QVERIFY2(err.isEmpty(), qPrintable(err));  // tool-level error, not a registry dispatch error
        QVERIFY(r.contains("error"));
        QVERIFY2(r["error"].toString().contains("not found"), qPrintable(r["error"].toString()));

        settings.clear();
    }

    // Corrupted stored data must be flagged, not silently reported as an
    // empty-but-healthy conversation — see silent-failure-hunter finding on
    // PR #1500: ai_conversations_list previously swallowed the parse error.
    void mcpAiConversationsList_corruptedEntry_flagsInsteadOfSwallowing()
    {
        AppSettings settings;
        settings.clear();

        QNetworkAccessManager nam;
        Settings appSettings;
        AIManager mgr(&nam, &appSettings);

        ShotProjection sw;
        sw.beanBrand = QStringLiteral("Brand");
        sw.beanType = QStringLiteral("Type");
        sw.profileName = QStringLiteral("Profile");
        const QString key = mgr.switchConversation(QVariant::fromValue(sw));
        // The index entry exists (switchConversation added it) but the
        // messages blob itself is garbage — simulating an interrupted write.
        settings.setValue(QStringLiteral("ai/conversations/") + key + "/messages",
                           QByteArrayLiteral("{not valid json"));

        McpToolRegistry registry;
        registerAIConversationTools(&registry, &mgr);

        QString err;
        const QJsonObject listResult = callConversations(registry, {{"action", "list"}}, err);
        const QJsonArray conversations = listResult["conversations"].toArray();
        QCOMPARE(conversations.size(), 1);
        const QJsonObject entry = conversations[0].toObject();
        QVERIFY2(entry["corrupted"].toBool(),
                 "corrupted transcript must be flagged, not silently reported as messageCount:0");
        QCOMPARE(entry["messageCount"].toInt(), 0);

        const QJsonObject getResult = callConversations(registry, {{"action", "get"}, {"key", key}}, err);
        QVERIFY(getResult.contains("error"));
        QVERIFY2(getResult["error"].toString().contains("Corrupted"),
                 qPrintable(getResult["error"].toString()));

        settings.clear();
    }

    // A key with no matching conversationIndex entry (evicted, or a legacy
    // conversation predating the index) must still return a real
    // lastUpdated by falling back to the per-conversation stored timestamp,
    // instead of silently returning it blank.
    void mcpAiConversationGet_orphanedKey_fallsBackToStoredTimestamp()
    {
        AppSettings settings;
        settings.clear();

        // AIManager's constructor runs a one-time clearAllConversationsOnce
        // migration that wipes the whole "ai/conversations" QSettings group
        // when its marker is absent (settings.clear() above wiped it too) —
        // construct AIManager BEFORE writing the orphaned key, or this
        // migration wipes it out from under the test.
        QNetworkAccessManager nam;
        Settings appSettings;
        AIManager mgr(&nam, &appSettings);  // conversationIndex() has no entry for `key`

        const QString key = QStringLiteral("orphaned_test_key");
        const QString prefix = QStringLiteral("ai/conversations/") + key + "/";
        settings.setValue(prefix + "systemPrompt", QStringLiteral("system"));
        settings.setValue(prefix + "messages", QJsonDocument(QJsonArray{
            QJsonObject{{"role", "user"}, {"content", "hi"}}
        }).toJson(QJsonDocument::Compact));
        settings.setValue(prefix + "timestamp", QDateTime::currentDateTime().toString(Qt::ISODate));

        McpToolRegistry registry;
        registerAIConversationTools(&registry, &mgr);

        QString err;
        const QJsonObject getResult = callConversations(registry, {{"action", "get"}, {"key", key}}, err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QVERIFY(!getResult.contains("error"));
        const QJsonObject metadata = getResult["metadata"].toObject();
        QVERIFY2(metadata["beanBrand"].toString().isEmpty(),
                 "no index entry means bean identity is honestly unknown, not an error");
        QVERIFY2(!metadata["lastUpdated"].toString().isEmpty(),
                 "lastUpdated must fall back to the stored per-conversation timestamp "
                 "when the key has no conversationIndex entry");

        settings.clear();
    }

    // ---- conversation import remaps shot references (fix-restore-id-remap) --
    //
    // Importing a backup re-INSERTs every shot, so each gets a NEW id. The
    // conversations in that same backup name the OLD ones. Before this, they
    // were written through verbatim: on the reporting device the Aug 19 shot was
    // id 1052 while the conversation discussing it still said 1109. Ids only
    // climb, so a stale one eventually names a real but unrelated shot and the
    // advisor's write-back lands on the wrong row.

    static QJsonArray turnsWithShotIds(const QList<qint64>& ids)
    {
        QJsonArray msgs;
        for (qint64 id : ids) {
            QJsonObject u{{"role", "user"}, {"content", "q"}};
            QJsonObject a{{"role", "assistant"}, {"content", "a"}};
            if (id > 0) {
                u["shotId"] = static_cast<double>(id);
                a["shotId"] = static_cast<double>(id);
            }
            msgs.append(u);
            msgs.append(a);
        }
        return msgs;
    }

    static QJsonArray storedTurns(AppSettings& s, const QString& key)
    {
        return QJsonDocument::fromJson(
            s.value("ai/conversations/" + key + "/messages").toByteArray()).array();
    }

    // The key is DERIVED from the conversation's own fields, never chosen. The
    // importer re-derives it and refuses a conversation whose carried key does
    // not match, so a hand-picked literal would be read as an archive written
    // before the equipment package joined the key.
    static QString convKey(qint64 equipmentId)
    {
        return ConversationKey::derive(QStringLiteral("B"), QStringLiteral("T"),
                                       QStringLiteral("P"), equipmentId);
    }

    static QJsonArray oneConversation(qint64 equipmentId, const QJsonArray& msgs)
    {
        return QJsonArray{ QJsonObject{
            {"key", convKey(equipmentId)}, {"systemPrompt", "sys"}, {"contextLabel", "l"},
            {"timestamp", "2026-08-22T09:00:00"}, {"messages", msgs},
            {"beanBrand", "B"}, {"beanType", "T"}, {"profileName", "P"},
            {"equipmentId", QJsonValue(equipmentId)},
            {"equipmentLabel", "Niche Zero / Decent 18g Ridged"}} };
    }

    void importedTurnsFollowTheShotRenumbering()
    {
        AppSettings settings;
        settings.clear();
        const QString key = convKey(0);

        QHash<qint64, qint64> map{{1109, 1052}, {1096, 1041}};
        const auto tally = AIConversation::importConversationsStatic(
            settings, oneConversation(0, turnsWithShotIds({1109, 1096})), &map, nullptr);

        QCOMPARE(tally.conversationsImported, 1);
        QCOMPARE(tally.turnsRemapped, 4);   // two turn pairs
        QCOMPARE(tally.turnsCleared, 0);

        const QJsonArray out = storedTurns(settings, key);
        QCOMPARE(out.size(), 4);
        for (const QJsonValue& v : out) {
            const qint64 id = static_cast<qint64>(v.toObject().value("shotId").toDouble());
            QVERIFY2(id == 1052 || id == 1041, "every turn must name a destination id");
        }
        settings.clear();
    }

    void aTurnWhoseShotDidNotComeAcrossLosesItsShotId()
    {
        AppSettings settings;
        settings.clear();
        const QString key = convKey(0);

        QHash<qint64, qint64> map{{1109, 1052}};   // 1096 absent — did not import
        const auto tally = AIConversation::importConversationsStatic(
            settings, oneConversation(0, turnsWithShotIds({1109, 1096})), &map, nullptr);

        QCOMPARE(tally.turnsRemapped, 2);
        QCOMPARE(tally.turnsCleared, 2);

        const QJsonArray out = storedTurns(settings, key);
        int withId = 0, withoutId = 0;
        for (const QJsonValue& v : out) {
            if (v.toObject().contains("shotId")) {
                withId++;
                QCOMPARE(static_cast<qint64>(v.toObject().value("shotId").toDouble()), qint64(1052));
            } else {
                withoutId++;
            }
        }
        QCOMPARE(withId, 2);
        // Absent, not shotId:0 — omission is this field's documented null state.
        QCOMPARE(withoutId, 2);
        settings.clear();
    }

    void conversationsImportedWithoutShotsHaveEveryIdCleared()
    {
        AppSettings settings;
        settings.clear();
        const QString key = convKey(0);

        // No map: the conversations-only import path. Those ids name a database
        // this device does not have, so keeping them is the defect.
        const auto tally = AIConversation::importConversationsStatic(
            settings, oneConversation(0, turnsWithShotIds({1109, 1096})), nullptr, nullptr);

        QCOMPARE(tally.turnsRemapped, 0);
        QCOMPARE(tally.turnsCleared, 4);
        for (const QJsonValue& v : storedTurns(settings, key))
            QVERIFY(!v.toObject().contains("shotId"));
        settings.clear();
    }

    void turnsThatNeverCarriedAShotIdAreLeftAlone()
    {
        AppSettings settings;
        settings.clear();
        const QString key = convKey(0);

        QHash<qint64, qint64> map{{1109, 1052}};
        const auto tally = AIConversation::importConversationsStatic(
            settings, oneConversation(0, turnsWithShotIds({0})), &map, nullptr);

        QCOMPARE(tally.turnsRemapped, 0);
        QCOMPARE(tally.turnsCleared, 0);
        for (const QJsonValue& v : storedTurns(settings, key))
            QVERIFY2(!v.toObject().contains("shotId"),
                     "a free-form turn must not have linkage invented for it");
        settings.clear();
    }

    // Task 6.4 as written: import a source whose ids exceed anything in the
    // destination, then assert NO STORED REFERENCE names a source id.
    //
    // Every other remap slot hand-writes its QHash, so nothing else joins the
    // two halves: a producer that keyed the map by destination id instead of
    // source would satisfy both sides' own tests and still corrupt every turn.
    void aRealImportMapCarriesStoredTurnsOntoDestinationIds()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        // Source: ids forced high, the way a mature device's backup looks.
        const QString srcPath = dir.filePath("e2e_src.db");
        QList<qint64> srcIds;
        {
            ShotHistoryStorage src;
            QVERIFY(src.initialize(srcPath));
            for (int i = 0; i < 40; i++) {
                ShotRecord r;
                r.summary.uuid = QStringLiteral("e2e-%1").arg(i);
                r.summary.timestamp = 1765300000 + i * 3600;
                r.summary.profileName = QStringLiteral("E2E %1").arg(i);
                r.summary.beverageType = QStringLiteral("espresso");
                r.pressure.append(QPointF(0.0, 6.0));
                const qint64 id = src.importShotRecord(r, false);
                QVERIFY(id > 0);
                if (i >= 37) srcIds << id;   // the three we will reference
            }
            src.close();
        }

        // The destination must already hold shots, and enough of them that the
        // ids it hands out cannot collide with the source's. An EMPTY
        // destination restarts AUTOINCREMENT at 1 and reproduces the source's
        // own ids exactly, which would make an un-remapped turn indistinguishable
        // from a correctly remapped one — the test would pass while asserting
        // nothing.
        const QString destPath = dir.filePath("e2e_dest.db");
        {
            ShotHistoryStorage dest;
            QVERIFY(dest.initialize(destPath));
            for (int i = 0; i < 60; i++) {
                ShotRecord d;
                d.summary.uuid = QStringLiteral("pad-%1").arg(i);
                d.summary.timestamp = 1760000000 + i * 3600;
                d.summary.profileName = QStringLiteral("Pad %1").arg(i);
                d.summary.beverageType = QStringLiteral("espresso");
                d.pressure.append(QPointF(0.0, 6.0));
                QVERIFY(dest.importShotRecord(d, false) > 0);
            }
            dest.close();
        }

        ShotHistoryStorage::ImportResult r;
        QVERIFY(ShotHistoryStorage::importDatabaseStatic(destPath, srcPath, true, &r));
        QCOMPARE(r.imported, 40);
        // Makes the literal 60 below self-checking rather than a remembered
        // number: if the padding ever collapses, this fails here and says so.
        QCOMPARE(r.destShotsBefore.value_or(-1), 60);
        // Every source id lands past the 60 already here, so nothing maps to
        // itself and a turn left un-remapped is detectable by value alone.
        for (qint64 sid : std::as_const(srcIds))
            QVERIFY2(r.shotIdMap.value(sid, 0) > 60, "imported shots must land past the padding");

        AppSettings settings;
        settings.clear();
        const QString key = convKey(0);
        AIConversation::importConversationsStatic(
            settings, oneConversation(0, turnsWithShotIds(srcIds)), &r.shotIdMap,
            r.equipmentIdMapOrNull());

        const QSet<qint64> sourceIds(srcIds.begin(), srcIds.end());
        int seen = 0;
        for (const QJsonValue& v : storedTurns(settings, key)) {
            const QJsonObject msg = v.toObject();
            if (!msg.contains("shotId")) continue;
            const qint64 id = static_cast<qint64>(msg.value("shotId").toDouble());
            QVERIFY2(!sourceIds.contains(id), "a stored turn still names a SOURCE shot id");
            QVERIFY2(r.shotIdMap.key(id, 0) != 0, "a stored turn names an id not in the map");
            seen++;
        }
        QVERIFY(!sourceIds.isEmpty());
        QCOMPARE(seen, srcIds.size() * 2);

        settings.clear();
    }

    // The read-time repair, end to end and with a REAL storage wired.
    //
    // Every other slot here calls the static helpers directly, and the
    // loadFromStorage pass is guarded on m_aiManager->shotHistoryStorage()
    // being non-null — so without this slot the whole block is unreachable in
    // the suite and could be deleted without a single test going red. It is
    // also the change's only remedy for installs already carrying the damage.
    void loadFromStorageForgetsTurnIdsThatNoLongerNameAShot()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(dir.filePath("repair.db")));

        ShotRecord rec;
        rec.summary.uuid = QStringLiteral("live-shot");
        rec.summary.timestamp = 1765200000;
        rec.summary.profileName = QStringLiteral("P");
        rec.summary.beverageType = QStringLiteral("espresso");
        rec.pressure.append(QPointF(0.0, 6.0));
        const qint64 liveId = storage.importShotRecord(rec, false);
        QVERIFY(liveId > 0);

        // Construct the manager BEFORE writing the conversation: AIManager's ctor
        // runs the one-time clearAllConversationsOnce migration (aimanager.cpp),
        // which wipes the whole ai/conversations group when its marker key is
        // absent — and the preceding slots' settings.clear() removes that marker,
        // so it fires here on every run.
        QNetworkAccessManager nam;
        Settings appSettings;
        AIManager mgr(&nam, &appSettings);
        mgr.setShotHistoryStorage(&storage);

        AppSettings settings;
        const QString key = QStringLiteral("repair_key");
        // One turn pair naming the shot that exists, one naming an id that does
        // not — the shape a pre-remap restore leaves behind.
        settings.setValue("ai/conversations/" + key + "/messages",
                          QJsonDocument(turnsWithShotIds({liveId, 987654})).toJson(QJsonDocument::Compact));
        settings.sync();   // loadFromStorage opens its own AppSettings

        AIConversation conv(&mgr);
        conv.setStorageKey(key);
        conv.loadFromStorage();

        QCOMPARE(conv.m_messages.size(), qsizetype(4));   // the fixture loaded at all
        int live = 0, stale = 0;
        for (const QJsonValue& v : std::as_const(conv.m_messages)) {
            const QJsonObject msg = v.toObject();
            if (!msg.contains("shotId")) continue;
            const qint64 id = static_cast<qint64>(msg.value("shotId").toDouble());
            if (id == liveId) live++;
            else stale++;
        }
        QCOMPARE(live, 2);
        QCOMPARE(stale, 0);

        // Idempotent, and the clean-outcome log states the right COUNT. That
        // line reported 0 when first written — it read `referenced` after the
        // set had been reduced to the unresolvable ids, so it announced "all
        // resolve" beside the number that did not. ignoreMessage fails on an
        // unmatched pattern, so this pins the wording and the number.
        QTest::ignoreMessage(QtDebugMsg,
                             QRegularExpression("all 1 distinct turn shot reference\\(s\\) resolve"));
        conv.repairStaleTurnShotIds();
        int stillLive = 0;
        for (const QJsonValue& v : std::as_const(conv.m_messages))
            if (v.toObject().contains("shotId")) stillLive++;
        QCOMPARE(stillLive, 2);

        settings.clear();
        storage.close();
    }

    // The same pass must LEAVE THE DATA ALONE when the database cannot answer.
    // An un-initialized storage returns nullopt, not an empty set; if the two
    // were collapsed, every reference here would be deleted — and, since
    // saveToStorage persists the result, so the drop is permanent. This is the slot that goes red if existingShotIds ever regresses to
    // returning a bare QSet.
    void loadFromStorageLeavesTurnIdsAloneWhenTheDatabaseCannotAnswer()
    {
        ShotHistoryStorage notReady;   // never initialize()d

        // Manager first, for the same reason as the slot above:
        // clearAllConversationsOnce wipes ai/conversations on construction.
        QNetworkAccessManager nam;
        Settings appSettings;
        AIManager mgr(&nam, &appSettings);
        mgr.setShotHistoryStorage(&notReady);

        AppSettings settings;
        const QString key = QStringLiteral("unanswerable_key");
        settings.setValue("ai/conversations/" + key + "/messages",
                          QJsonDocument(turnsWithShotIds({1109, 1052})).toJson(QJsonDocument::Compact));
        settings.sync();   // loadFromStorage opens its own AppSettings

        AIConversation conv(&mgr);
        conv.setStorageKey(key);
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression("could not check turn shot references"));
        conv.loadFromStorage();

        QCOMPARE(conv.m_messages.size(), qsizetype(4));   // the fixture loaded at all
        int kept = 0;
        for (const QJsonValue& v : std::as_const(conv.m_messages))
            if (v.toObject().contains("shotId")) kept++;
        QCOMPARE(kept, 4);

        settings.clear();
    }

    // The rating the user typed at the advisor went nowhere, and until this
    // change nothing said so. The signal is the mechanism for telling them;
    // without a test it could be emitted for the wrong write, or not at all,
    // and the suite would stay green.
    void aRatingWrittenToAMissingShotReportsFailure()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(dir.filePath("capture_fail.db")));

        QNetworkAccessManager nam;
        Settings appSettings;
        AIManager mgr(&nam, &appSettings);
        mgr.setShotHistoryStorage(&storage);

        QSignalSpy failed(&mgr, &AIManager::shotMetadataCaptureFailed);

        // 8473 does not exist in a database that has never held a shot — the
        // shape a stale conversation reference produces. Both layers warn: the
        // storage layer that the row is not there, and AIManager that the
        // user's answer was lost. Declare both rather than going blind.
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression("No shot with id 8473 to update"));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("did not land"));
        mgr.maybePersistRatingFromReply(
            QStringLiteral("82, balanced"),
            QStringLiteral("How did this taste?"),
            /*shotId=*/8473);

        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 5000);
        QCOMPARE(failed.first().at(0).toLongLong(), qint64(8473));

        storage.close();
    }

    // One reply drives TWO metadata writes for the same shot — a rating and a
    // bean correction, called back-to-back from AIConversation. That premise is
    // why m_pendingMetadataWrites is a refcount: as a QSet the first outcome
    // consumed the only entry and the second, possibly the failing one, was
    // discarded as "not ours" by the very filter added to catch failures.
    //
    // Driven through both PRODUCERS, not by poking the map. An earlier version
    // wrote `m_pendingMetadataWrites[8473] += 2` by hand, which covered the
    // decrement arithmetic but would have stayed green if either producer
    // stopped registering its write at all — and the bean-correction producer
    // had no other coverage.
    void twoWritesToOneShotBothReportTheirOutcome()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(dir.filePath("capture_two.db")));

        QNetworkAccessManager nam;
        Settings appSettings;
        AIManager mgr(&nam, &appSettings);
        mgr.setShotHistoryStorage(&storage);

        QSignalSpy failed(&mgr, &AIManager::shotMetadataCaptureFailed);

        const QString prior = QStringLiteral("How did this taste?");
        const QString reply = QStringLiteral("82, actually the roast is dark");

        mgr.maybePersistRatingFromReply(reply, prior, 8473);
        mgr.maybePersistBeanCorrectionFromReply(reply, prior, 8473);
        // The premise of the refcount, asserted rather than assumed: both
        // producers registered a write against the same shot id.
        QCOMPARE(mgr.m_pendingMetadataWrites.value(8473), 2);

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("No shot with id 8473 to update"));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("No shot with id 8473 to update"));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("did not land"));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("did not land"));

        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 2, 5000);
        QVERIFY(!mgr.m_pendingMetadataWrites.contains(8473));

        storage.close();
    }

    // The shared importer skips a conversation whose key this device already
    // has, rather than merging or overwriting. Untested until now: every other
    // tally slot clears settings first, so existingKeys was always empty and the
    // branch was dead in the suite. It is a user-data-overwrite shape on code
    // that was just collapsed from three hand-written copies.
    void anExistingConversationIsSkippedWholeNotOverwritten()
    {
        AppSettings settings;
        settings.clear();
        // Two packages on one bean and profile — which is exactly what the key
        // separates, and the only way two conversations can differ here.
        const QString mine = convKey(0);
        const QString fresh = convKey(11);

        // What this device already holds, with its own turn text.
        QJsonArray liveMsgs = turnsWithShotIds({1052});
        QJsonObject firstTurn = liveMsgs.at(0).toObject();
        firstTurn["content"] = QStringLiteral("the copy already on this device");
        liveMsgs[0] = firstTurn;
        settings.setValue("ai/conversations/" + mine + "/messages",
                          QJsonDocument(liveMsgs).toJson(QJsonDocument::Compact));
        settings.setValue(QStringLiteral("ai/conversations/index"),
                          QJsonDocument(QJsonArray{QJsonObject{{"key", mine}}})
                              .toJson(QJsonDocument::Compact));

        // The archive carries an older copy of that same conversation, plus one
        // this device has never seen.
        QJsonArray incoming = oneConversation(0, turnsWithShotIds({1109}));
        for (const QJsonValue& v : oneConversation(4, turnsWithShotIds({1109})))
            incoming.append(v);

        QHash<qint64, qint64> map{{1109, 1052}};
        QHash<qint64, qint64> equipMap{{4, 11}};
        const auto tally =
            AIConversation::importConversationsStatic(settings, incoming, &map, &equipMap);

        QCOMPARE(tally.conversationsImported, 1);
        // Only the fresh conversation's turns were touched.
        QCOMPARE(tally.turnsRemapped, 2);

        QCOMPARE(storedTurns(settings, mine).at(0).toObject().value("content").toString(),
                 QStringLiteral("the copy already on this device"));
        QCOMPARE(storedTurns(settings, fresh).size(), qsizetype(2));

        // The index gained the new key and kept the old one.
        const QJsonArray index = QJsonDocument::fromJson(
            settings.value(QStringLiteral("ai/conversations/index")).toByteArray()).array();
        QSet<QString> keys;
        for (const QJsonValue& v : index) keys.insert(v.toObject().value("key").toString());
        QCOMPARE(keys, QSet<QString>({mine, fresh}));

        settings.clear();
    }

    // ---- import re-keys a conversation onto the destination package ---------
    //
    // The key holds the equipment package's ROW ID, and a restore renumbers
    // those rows. Writing the archive's key through verbatim leaves a thread in
    // the index that no shot on this device ever opens — and if the destination
    // happens to hold a package with the source's row id, one that opens on the
    // wrong basket.

    void aRestoredConversationIsRekeyedToTheDestinationPackage()
    {
        AppSettings settings;
        settings.clear();

        QHash<qint64, qint64> equipMap{{4, 11}};   // package 4 landed as 11
        const auto tally = AIConversation::importConversationsStatic(
            settings, oneConversation(4, turnsWithShotIds({0})), nullptr, &equipMap);

        QCOMPARE(tally.conversationsImported, 1);
        QCOMPARE(storedTurns(settings, convKey(11)).size(), qsizetype(2));
        QVERIFY2(storedTurns(settings, convKey(4)).isEmpty(),
                 "the source key names a package row this device does not have");

        // The index entry has to agree with the key it is filed under, or the
        // conversation list names one package while the thread belongs to
        // another.
        const QJsonArray index = QJsonDocument::fromJson(
            settings.value(QStringLiteral("ai/conversations/index")).toByteArray()).array();
        QCOMPARE(index.size(), 1);
        const QJsonObject entry = index.at(0).toObject();
        QCOMPARE(entry.value("key").toString(), convKey(11));
        QCOMPARE(entry.value("equipmentId").toVariant().toLongLong(), qint64(11));
        QCOMPARE(entry.value("equipmentLabel").toString(),
                 QStringLiteral("Niche Zero / Decent 18g Ridged"));

        settings.clear();
    }

    void aConversationNamingAPackageThatDidNotComeAcrossIsNotImported()
    {
        AppSettings settings;
        settings.clear();

        QHash<qint64, qint64> equipMap{{9, 11}};   // package 4 is not in it
        const auto tally = AIConversation::importConversationsStatic(
            settings, oneConversation(4, turnsWithShotIds({0})), nullptr, &equipMap);

        QCOMPARE(tally.conversationsImported, 0);
        // Not demoted to the unpackaged pool: that would file a thread about one
        // basket under "no basket", which is the mixing the key exists to stop.
        QVERIFY(storedTurns(settings, convKey(0)).isEmpty());
        QVERIFY(storedTurns(settings, convKey(4)).isEmpty());
        QVERIFY(settings.value(QStringLiteral("ai/conversations/index")).toByteArray().isEmpty());

        settings.clear();
    }

    void anUnpackagedConversationKeepsItsKeyAcrossTheImport()
    {
        AppSettings settings;
        settings.clear();

        QHash<qint64, qint64> equipMap{{4, 11}};
        const auto tally = AIConversation::importConversationsStatic(
            settings, oneConversation(0, turnsWithShotIds({0})), nullptr, &equipMap);

        // Bucket 0 is the unpackaged pool on both devices, so there is nothing
        // to renumber and the thread stays reachable.
        QCOMPARE(tally.conversationsImported, 1);
        QCOMPARE(storedTurns(settings, convKey(0)).size(), qsizetype(2));

        settings.clear();
    }

    void anArchiveKeyedBeforeEquipmentIsNotImported()
    {
        AppSettings settings;
        settings.clear();

        // What a pre-change archive carries: a key derived from bean and
        // profile alone. No shot on this device derives that key any more, so
        // importing it only occupies an index slot.
        QJsonArray legacy = oneConversation(0, turnsWithShotIds({0}));
        QJsonObject conv = legacy.at(0).toObject();
        conv["key"] = QStringLiteral("0123456789abcdef");
        conv.remove(QStringLiteral("equipmentId"));
        legacy[0] = conv;

        const auto tally = AIConversation::importConversationsStatic(
            settings, legacy, nullptr, nullptr);

        QCOMPARE(tally.conversationsImported, 0);
        QVERIFY(storedTurns(settings, QStringLiteral("0123456789abcdef")).isEmpty());
        QVERIFY(storedTurns(settings, convKey(0)).isEmpty());

        settings.clear();
    }

    // The writer and the reader are one pair, tested as one. Backup and restore
    // are the same field list read twice; the export used to be hand-written at
    // two call sites and a field added to one of them is invisible until a user
    // restores an archive and finds a conversation attached to nothing.
    void anExportedConversationCarriesItsPackageBackThroughTheImport()
    {
        AppSettings settings;
        settings.clear();

        const QString sourceKey = convKey(4);
        const QString prefix = QStringLiteral("ai/conversations/") + sourceKey + "/";
        settings.setValue(prefix + "messages",
                          QJsonDocument(turnsWithShotIds({0})).toJson(QJsonDocument::Compact));
        settings.setValue(prefix + "systemPrompt", QStringLiteral("sys"));
        settings.setValue(prefix + "timestamp", QStringLiteral("2026-08-22T09:00:00"));
        settings.setValue(QStringLiteral("ai/conversations/index"),
                          QJsonDocument(QJsonArray{QJsonObject{
                              {"key", sourceKey}, {"beanBrand", "B"}, {"beanType", "T"},
                              {"profileName", "P"}, {"equipmentId", QJsonValue(qint64(4))},
                              {"equipmentLabel", "Niche Zero / Decent 18g Ridged"},
                              {"timestamp", QJsonValue(qint64(1766000000))}}})
                              .toJson(QJsonDocument::Compact));

        const QJsonArray exported = AIConversation::exportConversationsStatic(settings);
        QCOMPARE(exported.size(), 1);

        settings.clear();   // the destination device
        QHash<qint64, qint64> equipMap{{4, 11}};
        const auto tally =
            AIConversation::importConversationsStatic(settings, exported, nullptr, &equipMap);

        QCOMPARE(tally.conversationsImported, 1);
        QCOMPARE(storedTurns(settings, convKey(11)).size(), qsizetype(2));

        settings.clear();
    }

    // A repaired conversation must STAY repaired when saveToStorage reconciles
    // against another writer's on-disk copy. That copy predates the repair and
    // still carries the stale ids, and adopting it verbatim silently undid the
    // whole thing — on exactly the installs that have a second writer, i.e. MCP
    // ai_advisor_invoke.
    void aRepairedConversationStaysRepairedAcrossAReconcilingSave()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(dir.filePath("reconcile.db")));

        ShotRecord rec;
        rec.summary.uuid = QStringLiteral("reconcile-shot");
        rec.summary.timestamp = 1765400000;
        rec.summary.profileName = QStringLiteral("R");
        rec.summary.beverageType = QStringLiteral("espresso");
        rec.pressure.append(QPointF(0.0, 6.0));
        const qint64 liveId = storage.importShotRecord(rec, false);
        QVERIFY(liveId > 0);

        QNetworkAccessManager nam;
        Settings appSettings;
        AIManager mgr(&nam, &appSettings);
        mgr.setShotHistoryStorage(&storage);

        AppSettings settings;
        const QString key = QStringLiteral("reconcile_key");
        settings.setValue("ai/conversations/" + key + "/messages",
                          QJsonDocument(turnsWithShotIds({liveId, 987654})).toJson(QJsonDocument::Compact));
        settings.sync();

        AIConversation conv(&mgr);
        conv.setStorageKey(key);
        conv.loadFromStorage();
        QCOMPARE(conv.m_messages.size(), qsizetype(4));

        // Another writer appends to the same key while we hold the repaired
        // copy — the ai_advisor_invoke path. Its array still carries 987654.
        QJsonArray onDisk = turnsWithShotIds({liveId, 987654});
        onDisk.append(QJsonObject{{"role", "user"}, {"content", "appended elsewhere"}});
        settings.setValue("ai/conversations/" + key + "/messages",
                          QJsonDocument(onDisk).toJson(QJsonDocument::Compact));
        settings.sync();

        conv.saveToStorage();

        AppSettings after;
        for (const QJsonValue& v : storedTurns(after, key)) {
            const QJsonObject msg = v.toObject();
            if (!msg.contains("shotId")) continue;
            QCOMPARE(static_cast<qint64>(msg.value("shotId").toDouble()), liveId);
        }

        settings.clear();
        storage.close();
    }

    void dropUnresolvableShotIdsForgetsIdsWithNoMatchingShot()
    {
        QJsonArray msgs = turnsWithShotIds({1109, 1052});
        const QSet<qint64> live{1052};

        QCOMPARE(AIConversation::dropUnresolvableShotIds(msgs, live), 2);

        int kept = 0;
        for (const QJsonValue& v : std::as_const(msgs)) {
            if (!v.toObject().contains("shotId")) continue;
            kept++;
            QCOMPARE(static_cast<qint64>(v.toObject().value("shotId").toDouble()), qint64(1052));
        }
        QCOMPARE(kept, 2);
    }
    // The three scenarios in specs/advisor-conversation-history/spec.md.
    //
    // This is the requirement that repairs the reported case. A saved thread
    // replays its stored turns on every request, so scoping the payload alone
    // leaves older turns describing another basket inside the same transcript,
    // still informing every answer. The key is what keeps a transcript describing
    // one equipment set for its whole life.
    void conversationKey_separatesEquipmentPackages()
    {
        const QString bean = QStringLiteral("Sweet Bloom Coffee");
        const QString type = QStringLiteral("Hometown Blend");
        const QString profile = QStringLiteral("D-Flow / Q");

        auto keyFor = [&](qint64 bucket) {
            ShotProjection p;
            p.beanBrand = bean; p.beanType = type; p.profileName = profile;
            p.equipmentId = bucket;
            return AIManager::conversationKey(p);
        };
        const QString decent = keyFor(3);
        const QString graph  = keyFor(2);

        QVERIFY2(decent != graph,
                 "same bean and profile on two equipment packages resolved to one thread — "
                 "the package-A turns would replay into the package-B conversation");

        // Returning to a package resumes its own thread, not a third one.
        QCOMPARE(keyFor(3), decent);

        // Every pre-change thread was keyed without a package. Bucket 0 is the
        // unpackaged pool, so a packaged shot must not land on one of those keys.
        const QString unpackaged = keyFor(0);
        QVERIFY2(unpackaged != decent && unpackaged != graph,
                 "a packaged shot resolved to the unpackaged thread — pre-change "
                 "conversations would be resumed rather than retired");
    }

};

QTEST_GUILESS_MAIN(tst_AIManager)

#include "tst_aimanager.moc"
