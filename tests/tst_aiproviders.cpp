// tst_aiproviders — pins the per-provider model-catalog contract that the
// AI Advisor's model picker and settings round-trip depend on.
//
// Each provider that offers a user-selectable model (OpenAI, Anthropic,
// Gemini, OpenRouter) exposes availableModels() as the single source of truth for both
// the UI list and the wire model. AIManager reads Settings.ai.providerModel()
// (which may be empty when unset, or a stale id after a catalog change) and
// feeds it to setModel() on every settings change, so the guard branches
// below are exercised in production on a routine basis:
//   - empty id      → keep the current model (constructor default when unset)
//   - unknown id    → warn + keep the current model (never send a dead id)
//   - valid id      → switch the wire model; shortModelName() tracks it
//   - construction  → default to availableModels().first().id
//   - modelHint()   → non-empty and mentions every catalog entry by name
//
// Those catalog methods are pure (no network I/O) and public, so no mocking or
// friend-class access is needed.
//
// The suite also pins the Anthropic REQUEST SHAPE (#1691), which does need a
// canned-response server: what broke there was an absent field in the posted
// JSON, not anything a pure method exposes. See FakeProviderServer below.

#include <QtTest>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QList>
#include <QPair>
#include <QString>

#include <functional>
#include <memory>

#include "ai/aiprovider.h"
#include "ai/airequestshape.h"
#include "helpers/diagnosticcapture.h"
#include "core/translationmanager.h"

// Canned-response HTTP server for the provider request/reply contracts below.
// Records the request BODY (not just the request line) because what these
// tests assert is what we send in the JSON — the #1691 regression was an
// absent field, invisible from the URL. Same shape as FakeBeanBaseServer in
// tst_beanbaseclient.cpp, which handles GETs and so needed no body
// accumulation.
//
// Provider-agnostic: every provider here speaks JSON over POST, so only the
// canned response body differs.
//
// NOTE: no RAW string literals (R"(...)") in this file. Ordinary escaped
// literals are fine and are used throughout — including brace-heavy JSON in
// the canned bodies below, which is why the rule has to name the raw form
// specifically rather than "string literals".
class FakeProviderServer : public QObject {
    Q_OBJECT
public:
    FakeProviderServer() {
        connect(&m_server, &QTcpServer::newConnection, this, [this]() {
            while (m_server.hasPendingConnections()) {
                QTcpSocket* sock = m_server.nextPendingConnection();
                // shared_ptr, captured BY VALUE in both lambdas: a raw
                // new/delete pair split across readyRead and disconnected is
                // both a use-after-free (readyRead can fire after disconnected
                // for data already in the read buffer) and a leak (QTcpServer
                // parents its sockets, so server destruction can reap the
                // socket before disconnected is ever delivered). The leak half
                // is invisible on macOS — no LSan — and would surface only in
                // the nightly Linux ASan job.
                auto buf = std::make_shared<QByteArray>();
                auto responded = std::make_shared<bool>(false);
                connect(sock, &QTcpSocket::readyRead, this, [this, sock, buf, responded]() {
                    if (*responded) return;  // one response per connection
                    buf->append(sock->readAll());
                    // Wait for the whole body: a POST can arrive in several
                    // chunks, and a half-read body parses as invalid JSON.
                    const qsizetype headerEnd = buf->indexOf("\r\n\r\n");
                    if (headerEnd < 0) return;
                    const QByteArray headers = buf->left(headerEnd);
                    // Header names are case-insensitive by spec. Qt title-cases
                    // them at serialization (qhttpnetworkrequest.cpp), so the
                    // exact spelling is stable today — but matching on a
                    // lowered copy costs nothing and won't hang every test here
                    // for 5s if that ever changes. (QByteArray::indexOf has no
                    // case-insensitive overload, hence the copy; toLower() is
                    // length-preserving for ASCII so the offset still applies
                    // to the original.)
                    static const QByteArray kContentLength = "content-length: ";
                    const qsizetype clPos = headers.toLower().indexOf(kContentLength);
                    if (clPos < 0) return;
                    const qsizetype expected =
                        headers.mid(clPos + kContentLength.size()).split('\r').first().toLongLong();
                    const QByteArray body = buf->mid(headerEnd + 4);
                    if (body.size() < expected) return;

                    *responded = true;
                    m_requestBodies.append(body);
                    const int status = m_nextStatus;
                    m_nextStatus = 200;
                    const QByteArray resp =
                        "HTTP/1.1 " + QByteArray::number(status) + " Test\r\n"
                        "Content-Type: application/json\r\n"
                        "Content-Length: " + QByteArray::number(m_responseBody.size()) + "\r\n"
                        "Connection: close\r\n"
                        "\r\n" + m_responseBody;
                    sock->write(resp);
                    sock->disconnectFromHost();
                });
                connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
            }
        });
        // Not Q_ASSERT: tests are also built in Release for the tag-push
        // linux-release job, where it compiles out — a failed listen() would
        // then point baseUrl() at port 0 and every test here would fail on a
        // 5s timeout with no clue why.
        if (!m_server.listen(QHostAddress::LocalHost, 0))
            qFatal("FakeProviderServer: listen() failed: %s",
                   qPrintable(m_server.errorString()));
    }

    QString baseUrl() const {
        return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort());
    }

    void respondWith(const QByteArray& body) { m_responseBody = body; }
    void failNextRequest(int status) { m_nextStatus = status; }

    // Body of the last request the provider actually sent, parsed as JSON.
    QJsonObject lastRequest() const {
        if (m_requestBodies.isEmpty()) return {};
        return QJsonDocument::fromJson(m_requestBodies.last()).object();
    }
    qsizetype requestCount() const { return m_requestBodies.size(); }

private:
    QTcpServer m_server;
    QByteArray m_responseBody = "{\"content\":[{\"type\":\"text\",\"text\":\"ok\"}],\"stop_reason\":\"end_turn\"}";
    QList<QByteArray> m_requestBodies;
    int m_nextStatus = 200;
};

namespace {

using Catalog = QList<QPair<QString, QString>>;  // (id, displayName), UI order

// Exercise the full catalog + setModel guard contract for one concrete
// provider type against its expected catalog. Templated because setModel()
// is declared per-derived-class, not as a base virtual.
template <typename ProviderT>
void checkProvider(QNetworkAccessManager& nam, const Catalog& expected)
{
    ProviderT p(&nam, QString(), nullptr);

    // availableModels() is the catalog, in UI order.
    const QList<AIProvider::ModelOption> models = p.availableModels();
    QCOMPARE(models.size(), expected.size());
    for (qsizetype i = 0; i < expected.size(); ++i) {
        QCOMPARE(models[i].id, expected[i].first);
        QCOMPARE(models[i].displayName, expected[i].second);
    }

    // Constructor defaults the wire model to the first (recommended) entry —
    // the "single source of truth" claim the UI's unset→index-0 fallback relies on.
    QCOMPARE(p.modelName(), expected.first().first);
    QCOMPARE(p.shortModelName(), expected.first().second);

    // modelHint() is the guidance line shown in the app's AI settings and on
    // the ShotServer page, and it must mention every catalog
    // entry by display name — a catalog bump that forgets the hint would ship
    // stale model-comparison advice to both UIs at once.
    const QString hint = p.modelHint();
    QVERIFY2(!hint.isEmpty(), "provider must provide a modelHint()");
    for (const AIProvider::ModelOption& opt : models) {
        QVERIFY2(hint.contains(opt.displayName),
                 qPrintable(QStringLiteral("modelHint() does not mention catalog model '%1'")
                                .arg(opt.displayName)));
    }

    // Selecting the last model switches the wire model and its label.
    const QString optId = expected.last().first;
    const QString optName = expected.last().second;
    p.setModel(optId);
    QCOMPARE(p.modelName(), optId);
    QCOMPARE(p.shortModelName(), optName);

    // Unknown id (stale/renamed stored value) warns and is ignored — never
    // clobbers the current model with a dead id that would 400 every request.
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("ignoring unknown model id"));
    p.setModel(QStringLiteral("model-that-does-not-exist"));
    QCOMPARE(p.modelName(), optId);

    // Empty id (setting cleared, e.g. by a restore) returns to the default the
    // pickers show, rather than keeping a model the user no longer sees.
    p.setModel(QString());
    QCOMPARE(p.modelName(), expected.first().first);
}

} // namespace

class tst_AIProviders : public QObject {
    Q_OBJECT

private slots:
    void retryKeepsOperationIdentityAndOneSuccess()
    {
        DiagnosticCapture logs;
        QNetworkAccessManager nam;
        FakeProviderServer server;
        server.respondWith("{\"choices\":[{\"message\":{\"content\":\"ok\"},\"finish_reason\":\"stop\"}]}");
        server.failNextRequest(503);
        OpenRouterProvider provider(&nam, "test-key");
        provider.setBaseUrl(server.baseUrl());
        const auto operation = AIOperationLog::begin("advisor", false, 0, 71);
        operation->useProvider("openrouter", "fake/model", "providerText");
        provider.setDiagnosticOperation(operation);
        connect(&provider, &AIProvider::analysisComplete, this,
                [operation](const QString&) { operation->finish("success", "adviceReady"); });
        QSignalSpy completed(&provider, &AIProvider::analysisComplete);
        QSignalSpy failed(&provider, &AIProvider::analysisFailed);
        provider.analyze("secret-system", "secret-page");
        QVERIFY(completed.wait(5000));
        QCOMPARE(server.requestCount(), 2);
        QCOMPARE(failed.size(), 0);
        QCOMPARE(logs.terminals().size(), 1);
        QVERIFY(logs.terminals().first().contains("outcome=success"));
        const auto text = logs.lines().join('\n');
        QVERIFY(text.contains("httpStatus=503"));
        QVERIFY(text.contains("httpStatus=200"));
        QVERIFY(text.contains("retry 1"));
        QVERIFY(!text.contains("secret-"));
        for (const auto& line : logs.lines())
            if (line.contains("op="))
                QVERIFY(line.contains("op=" + operation->id));
    }

    void upstreamErrorContentNeverEntersDiagnostics()
    {
        DiagnosticCapture logs;
        QNetworkAccessManager nam;
        FakeProviderServer server;
        server.respondWith("{\"choices\":[{\"error\":{\"code\":\"secret-code\","
                           "\"message\":\"secret-prompt-and-api-key\"}}]}");
        OpenRouterProvider provider(&nam, "test-key");
        provider.setBaseUrl(server.baseUrl());
        const auto operation = AIOperationLog::begin("bagExtraction", true, 71);
        operation->useProvider("openrouter", "fake/model", "providerText");
        provider.setDiagnosticOperation(operation);
        QSignalSpy failed(&provider, &AIProvider::analysisFailed);
        provider.analyze("secret-system", "secret-page");
        QVERIFY(failed.wait(5000));
        QCOMPARE(server.requestCount(), 1);
        // The user's detailed error remains available, while the shared log omits it.
        QVERIFY(failed.first().first().toString().contains("secret-prompt"));
        operation->finish("failed", "providerFailure");
        const auto text = logs.lines().join('\n');
        QVERIFY(!text.contains("secret-"));
        QVERIFY(text.contains("httpStatus=200"));
        QVERIFY(text.contains("remoteErrorContentOmitted"));
        QCOMPARE(logs.terminals().size(), 1);
        QVERIFY(logs.terminals().first().startsWith("[BeanBase][Operation]"));
    }

    // The bulk translator keeps its own fallback model per provider (see
    // TranslationManager::fallbackTranslationModel) because decenza_testlib compiles the
    // translator but not the AI stack. This is the test that keeps the two in step.
    //
    // It exists because they went out of step and nobody noticed for months: the translator
    // asked Anthropic for claude-3-5-haiku-20241022 long after it was retired, and asked
    // OpenAI and Gemini for models the picker does not even offer. The failure was invisible
    // -- a dead provider just falls through to the next configured one.
    void translationFallbacksMatchTheProviderCatalogs()
    {
        QNetworkAccessManager nam;
        struct Case { const char* id; AIProvider* provider; };
        OpenAIProvider openai(&nam, QString());
        AnthropicProvider anthropic(&nam, QString());
        GeminiProvider gemini(&nam, QString());

        const QList<QPair<QString, AIProvider*>> cases = {
            {QStringLiteral("openai"), &openai},
            {QStringLiteral("anthropic"), &anthropic},
            {QStringLiteral("gemini"), &gemini},
        };

        for (const auto& c : cases) {
            const QList<AIProvider::ModelOption> models = c.second->availableModels();
            QVERIFY2(!models.isEmpty(), qPrintable(c.first + " has an empty model catalog"));

            const QString fallback = TranslationManager::fallbackTranslationModel(c.first);
            QVERIFY2(!fallback.isEmpty(),
                     qPrintable("no translation fallback declared for " + c.first));

            // Must be the FIRST entry: that is this codebase's definition of "recommended"
            // (see each provider's constructor), so the translator and the picker agree on
            // what the default is rather than merely both being valid.
            QCOMPARE(fallback, models.first().id);
        }
    }

    // Every catalogued model must be priced. costHintFor() matches model ids
    // EXACTLY and returns an empty string for anything it does not recognise,
    // deliberately: falling through to some other model's price quietly
    // promises a spend nobody computed, and the error is unbounded (Luna and
    // GPT-5.4 differ by 12x inside one catalog).
    //
    // That choice trades a wrong cost line for a MISSING one, and a missing
    // one is silent too -- the QML binds visible to text.length, so a model
    // added to availableModels() without a costHintFor() case just shows
    // nothing. This is the test that makes that loud instead. It is the same
    // catalog-drift shape as translationFallbacksMatchTheProviderCatalogs
    // above, one field over.
    void everyCataloguedModelHasACostHint()
    {
        QNetworkAccessManager nam;
        OpenAIProvider openai(&nam, QString());
        AnthropicProvider anthropic(&nam, QString());
        GeminiProvider gemini(&nam, QString());
        OpenRouterProvider openrouter(&nam, QString());

        for (AIProvider* provider : {static_cast<AIProvider*>(&openai),
                                     static_cast<AIProvider*>(&anthropic),
                                     static_cast<AIProvider*>(&gemini),
                                     static_cast<AIProvider*>(&openrouter)}) {
            const QList<AIProvider::ModelOption> models = provider->availableModels();
            QVERIFY(!models.isEmpty());
            for (const AIProvider::ModelOption& m : models) {
                QVERIFY2(!provider->costHintFor(m.id).isEmpty(),
                         qPrintable(QStringLiteral("catalogued model '%1' has no costHintFor() "
                                                   "case, so its cost line renders empty")
                                        .arg(m.id)));
            }
        }
    }

    // The other half of the same contract: an id that is NOT catalogued must
    // price as empty rather than inheriting a neighbour's figure.
    void unknownModelIdIsNotPriced()
    {
        QNetworkAccessManager nam;
        OpenAIProvider openai(&nam, QString());
        AnthropicProvider anthropic(&nam, QString());
        GeminiProvider gemini(&nam, QString());

        // "gemini-2-something" specifically: the Gemini case was a
        // startsWith("gemini-2") prefix match, which priced any future
        // gemini-2.x at 2.5 Flash's rate.
        QVERIFY(openai.costHintFor(QStringLiteral("gpt-9-imaginary")).isEmpty());
        QVERIFY(anthropic.costHintFor(QStringLiteral("claude-opus-9")).isEmpty());
        QVERIFY(gemini.costHintFor(QStringLiteral("gemini-2-something")).isEmpty());
        // Priced elsewhere, but not offered here: the cost table is shared.
        QVERIFY(openai.costHintFor(QStringLiteral("claude-sonnet-5-5")).isEmpty());
        OpenRouterProvider openrouter(&nam, QString());
        QVERIFY(openrouter.costHintFor(QStringLiteral("gpt-6-luna")).isEmpty());
        QVERIFY(openrouter.costHintFor(QStringLiteral("openai/gpt-9-imaginary")).isEmpty());
    }

    // Every catalogued model must send a thinking setting (and, on OpenAI, a
    // temperature or none) that was verified live (tools/ai_model_eval,
    // 2026-10-05 and -06): the accepted values differ by model and a wrong one
    // 400s every request. The table is that record; a model added to a catalog
    // fails here until it has been probed and recorded.
    void everyCataloguedModelSendsAVerifiedThinkingSetting()
    {
        const QHash<QString, QString> verified = {
            {"gpt-6.1-sol", "reasoning_effort=low temperature=no"},
            {"gpt-6-luna", "reasoning_effort=none temperature=yes"},
            {"claude-sonnet-5-5", "thinking=between_tools advisor=between_tools"},
            {"claude-haiku-5-5", "thinking=disabled advisor=adaptive/low"},
            {"gemini-3.8-flash", "thinkingLevel=low"},
            {"openai/gpt-6-luna", R"(reasoning={"effort":"none"})"},
            {"openai/gpt-6.1-sol", R"(reasoning={"effort":"low"})"},
            {"anthropic/claude-sonnet-5.5", R"(reasoning={"effort":"low"})"},
            {"anthropic/claude-haiku-5.5", R"(reasoning={"effort":"low"})"},
            {"google/gemini-3.8-flash", R"(reasoning={"effort":"low"})"},
            {"z-ai/glm-5.3-flash", R"(reasoning={"effort":"low"})"},
            {"google/gemma-4-31b-it", R"(reasoning={"enabled":true})"},
        };
        const auto sent = [](const QString& provider, const QString& model) {
            QJsonObject body;
            if (provider == QLatin1String("openai")) {
                AIRequestShape::disableOpenAIReasoning(body, model);
                AIRequestShape::setOpenAITemperature(body, model, 0.3);
                return "reasoning_effort=" + body["reasoning_effort"].toString()
                     + (body.contains("temperature") ? " temperature=yes" : " temperature=no");
            }
            if (provider == QLatin1String("anthropic")) {
                AIRequestShape::disableAnthropicThinking(body, model);
                QJsonObject advisor;
                AIRequestShape::setAnthropicAdvisorThinking(advisor, model);
                const QString effort = advisor["output_config"].toObject()["effort"].toString();
                return "thinking=" + body["thinking"].toObject()["type"].toString() + " advisor="
                     + advisor["thinking"].toObject()["type"].toString()
                     + (effort.isEmpty() ? QString() : "/" + effort);
            }
            if (provider == QLatin1String("openrouter")) {
                AIRequestShape::setOpenRouterReasoning(body, model);
                return "reasoning="
                     + QString::fromUtf8(QJsonDocument(body["reasoning"].toObject()).toJson(QJsonDocument::Compact));
            }
            const QJsonObject config = AIRequestShape::geminiThinkingConfig(model);
            return config.contains("thinkingBudget")
                ? "thinkingBudget=" + QString::number(config["thinkingBudget"].toInt())
                : "thinkingLevel=" + config["thinkingLevel"].toString();
        };
        QNetworkAccessManager nam;
        OpenAIProvider openai(&nam, QString());
        AnthropicProvider anthropic(&nam, QString());
        GeminiProvider gemini(&nam, QString());
        OpenRouterProvider openrouter(&nam, QString());
        for (AIProvider* provider : {static_cast<AIProvider*>(&openai), static_cast<AIProvider*>(&anthropic),
                                     static_cast<AIProvider*>(&gemini), static_cast<AIProvider*>(&openrouter)}) {
            for (const AIProvider::ModelOption& m : provider->availableModels()) {
                QVERIFY2(verified.contains(m.id),
                         qPrintable(m.id + " has no live-verified thinking setting; probe it first"));
                QCOMPARE(sent(provider->id(), m.id), verified.value(m.id));
            }
        }
    }

    // Every OpenRouter request path sends the model's setting from
    // AIRequestShape (whose values the verified table above pins).
    void openRouterRequestsCarryTheReasoningSetting()
    {
        QNetworkAccessManager nam;
        FakeProviderServer server;
        server.respondWith("{\"choices\":[{\"message\":{\"content\":\"ok\"},\"finish_reason\":\"stop\"}]}");
        OpenRouterProvider provider(&nam, "key");
        provider.setBaseUrl(server.baseUrl());
        QSignalSpy completed(&provider, &AIProvider::analysisComplete);
        QSignalSpy tested(&provider, &AIProvider::testResult);
        const QJsonArray turn{QJsonObject{{"role", "user"}, {"content", "hi"}}};
        for (const AIProvider::ModelOption& opt : provider.availableModels()) {
            provider.setModel(opt.id);
            QJsonObject expected;
            AIRequestShape::setOpenRouterReasoning(expected, opt.id);
            const std::function<void()> paths[] = {
                [&] { provider.analyze("system", "user"); QVERIFY(completed.wait(5000)); },
                [&] { provider.analyzeConversation("system", turn); QVERIFY(completed.wait(5000)); },
                [&] { provider.testConnection(); QVERIFY(tested.wait(5000)); },
            };
            for (const auto& send : paths) {
                send();
                const QJsonObject body = server.lastRequest();
                QCOMPARE(body["model"].toString(), opt.id);
                QCOMPARE(body["reasoning"].toObject(), expected["reasoning"].toObject());
            }
        }
    }

    void init() { QTest::failOnWarning(); }
    void openAiCatalogAndSelection()
    {
        QNetworkAccessManager nam;
        checkProvider<OpenAIProvider>(nam, {
            { "gpt-6.1-sol", "GPT-6.1 Sol" },
            { "gpt-6-luna", "GPT-6 Luna" },
        });
    }

    void anthropicCatalogAndSelection()
    {
        QNetworkAccessManager nam;
        checkProvider<AnthropicProvider>(nam, {
            { "claude-sonnet-5-5", "Sonnet 5.5" },
            { "claude-haiku-5-5", "Haiku 5.5" },
        });
    }

    void geminiCatalogAndSelection()
    {
        QNetworkAccessManager nam;
        checkProvider<GeminiProvider>(nam, {
            { "gemini-3.8-flash", "3.8 Flash" },
        });
    }

    void openRouterCatalogAndSelection()
    {
        QNetworkAccessManager nam;
        checkProvider<OpenRouterProvider>(nam, {
            { "openai/gpt-6-luna", "GPT-6 Luna" },
            { "openai/gpt-6.1-sol", "GPT-6.1 Sol" },
            { "anthropic/claude-sonnet-5.5", "Sonnet 5.5" },
            { "anthropic/claude-haiku-5.5", "Haiku 5.5" },
            { "google/gemini-3.8-flash", "Gemini 3.8 Flash" },
            { "z-ai/glm-5.3-flash", "GLM-5.3 Flash" },
            { "google/gemma-4-31b-it", "Gemma 4 31B" },
        });
    }

    // Stage-2 URL extraction feature matrix (add-recipe-wizard-tea): the
    // three cloud providers with a server-side web tool support analyzeUrl;
    // Ollama (local) and OpenRouter don't. ChangeBeansDialog gates the
    // stage-2 fallback on this flag, so a flip here is user-visible.
    void urlAnalysisSupportMatrix()
    {
        QNetworkAccessManager nam;
        QVERIFY(OpenAIProvider(&nam, "key").supportsUrlAnalysis());
        QVERIFY(AnthropicProvider(&nam, "key").supportsUrlAnalysis());
        QVERIFY(GeminiProvider(&nam, "key").supportsUrlAnalysis());
        QVERIFY(!OpenRouterProvider(&nam, "key").supportsUrlAnalysis());
        QVERIFY(!OllamaProvider(&nam, "http://localhost:11434", "model").supportsUrlAnalysis());
    }

    // #1691: every Anthropic request must send its thinking setting explicitly.
    //
    // Newer models run ADAPTIVE thinking when the field is omitted, and since
    // max_tokens bounds thinking + text together it could eat the whole budget,
    // leaving no text block at all. The catalog default, Sonnet 5.5, takes
    // "between_tools" as its off. The assertion has to be on the posted JSON.
    //
    // All four request paths, not just the reported one: deleting the call from
    // any single builder must fail this test.
    void anthropicRequestsDisableThinkingAndUseTheRaisedCap()
    {
        QNetworkAccessManager nam;
        FakeProviderServer server;
        AnthropicProvider p(&nam, QStringLiteral("key"));
        p.setBaseUrl(server.baseUrl());

        QSignalSpy complete(&p, &AIProvider::analysisComplete);

        p.analyze(QStringLiteral("system"), QStringLiteral("user"));
        QVERIFY(complete.wait(5000));
        QCOMPARE(server.requestCount(), 1);
        QJsonObject body = server.lastRequest();
        QCOMPARE(body["thinking"].toObject()["type"].toString(), QStringLiteral("between_tools"));
        QCOMPARE(body["max_tokens"].toInt(), 4096);

        // analyzeConversation() is the path the in-app advisor uses
        // (AIConversation::sendRequest).
        QJsonArray messages;
        QJsonObject userMsg;
        userMsg["role"] = QStringLiteral("user");
        userMsg["content"] = QStringLiteral("how did this shot taste?");
        messages.append(userMsg);
        p.analyzeConversation(QStringLiteral("system"), messages);
        QVERIFY(complete.wait(5000));
        QCOMPARE(server.requestCount(), 2);
        body = server.lastRequest();
        QCOMPARE(body["thinking"].toObject()["type"].toString(), QStringLiteral("between_tools"));
        QCOMPARE(body["max_tokens"].toInt(), 4096);

        // analyzeUrl() — the recipe-wizard stage-2 extraction path.
        p.analyzeUrl(QStringLiteral("system"), QStringLiteral("https://example.com/bag"));
        QVERIFY(complete.wait(5000));
        QCOMPARE(server.requestCount(), 3);
        body = server.lastRequest();
        QCOMPARE(body["thinking"].toObject()["type"].toString(), QStringLiteral("between_tools"));
        QCOMPARE(body["max_tokens"].toInt(), 4096);
    }

    // testConnection() is the diagnostic that LIED during #1691: it only checks
    // for an HTTP error, so it passed while every analysis request failed — the
    // user's key tested fine and the advisor was unusable. It also uses the
    // tightest budget in the file (10 tokens), so it is the first request to
    // break if thinking is ever re-enabled by accident.
    void anthropicTestConnectionAlsoDisablesThinking()
    {
        QNetworkAccessManager nam;
        FakeProviderServer server;
        AnthropicProvider p(&nam, QStringLiteral("key"));
        p.setBaseUrl(server.baseUrl());

        QSignalSpy tested(&p, &AIProvider::testResult);
        p.testConnection();
        QVERIFY(tested.wait(5000));

        const QJsonObject body = server.lastRequest();
        QCOMPARE(body["thinking"].toObject()["type"].toString(), QStringLiteral("between_tools"));
        QCOMPARE(body["max_tokens"].toInt(), 10);
    }

    // Haiku 5.5 thinks on the advisor's two paths only. Test Connection's
    // 10-token budget cannot hold thinking (#1691), and extraction (URL or
    // text) was never shown to need it, so wiring the advisor setting into
    // any of them must fail here.
    void anthropicHaikuThinksOnlyOnAdvisorPaths()
    {
        QNetworkAccessManager nam;
        FakeProviderServer server;
        AnthropicProvider p(&nam, QStringLiteral("key"));
        p.setBaseUrl(server.baseUrl());
        p.setModel(QStringLiteral("claude-haiku-5-5"));
        QSignalSpy complete(&p, &AIProvider::analysisComplete);
        QSignalSpy tested(&p, &AIProvider::testResult);
        const auto thinking = [&server] {
            const QJsonObject body = server.lastRequest();
            const QString effort = body["output_config"].toObject()["effort"].toString();
            return body["thinking"].toObject()["type"].toString() + (effort.isEmpty() ? QString() : "/" + effort);
        };

        p.analyze(QStringLiteral("system"), QStringLiteral("user"));
        QVERIFY(complete.wait(5000));
        QCOMPARE(thinking(), QStringLiteral("adaptive/low"));

        QJsonArray messages{QJsonObject{{"role", "user"}, {"content", "how did this shot taste?"}}};
        p.analyzeConversation(QStringLiteral("system"), messages);
        QVERIFY(complete.wait(5000));
        QCOMPARE(thinking(), QStringLiteral("adaptive/low"));

        p.analyzeUrl(QStringLiteral("system"), QStringLiteral("https://example.com/bag"));
        QVERIFY(complete.wait(5000));
        QCOMPARE(thinking(), QStringLiteral("disabled"));

        p.extract(QStringLiteral("system"), QStringLiteral("bag page text"));
        QVERIFY(complete.wait(5000));
        QCOMPARE(thinking(), QStringLiteral("disabled"));

        p.testConnection();
        QVERIFY(tested.wait(5000));
        QCOMPARE(thinking(), QStringLiteral("disabled"));
    }

    // The branch that matters: a reply that DID produce text but was cut off.
    //
    // This is the case the empty-text test cannot reach, and it is the
    // dangerous one — before the fix it was emitted as if complete, silently
    // missing the trailing nextShot JSON block (#1054) that
    // AIManager::parseStructuredNext reads. Reducing the production guard to
    // `if (text.isEmpty())` must fail HERE; it does not fail the empty case,
    // whose fixture satisfies both halves of the condition at once.
    void anthropicReportsAPartialReplyAsTruncated()
    {
        QNetworkAccessManager nam;
        FakeProviderServer server;
        server.respondWith("{\"content\":[{\"type\":\"text\",\"text\":\"Grind fine\"}],"
                           "\"stop_reason\":\"max_tokens\"}");
        AnthropicProvider p(&nam, QStringLiteral("key"));
        p.setBaseUrl(server.baseUrl());

        QSignalSpy failed(&p, &AIProvider::analysisFailed);
        QSignalSpy complete(&p, &AIProvider::analysisComplete);
        QTest::ignoreMessage(QtDebugMsg, QRegularExpression("Anthropic.*model"));

        // analyze() is machine-parsed, so its policy is Fail.
        p.analyze(QStringLiteral("system"), QStringLiteral("user"));
        QVERIFY(failed.wait(5000));
        QCOMPARE(failed.size(), 1);
        QVERIFY(failed.first().first().toString().contains(QStringLiteral("cut off")));
        QCOMPARE(complete.size(), 0);  // must not ALSO emit the partial
    }

    // ...but the same truncated reply on the conversation path is prose the
    // user reads, so it is shown with a notice instead of discarded.
    void anthropicShowsAPartialConversationReply()
    {
        QNetworkAccessManager nam;
        FakeProviderServer server;
        server.respondWith("{\"content\":[{\"type\":\"text\",\"text\":\"Grind fine\"}],"
                           "\"stop_reason\":\"max_tokens\"}");
        AnthropicProvider p(&nam, QStringLiteral("key"));
        p.setBaseUrl(server.baseUrl());

        QSignalSpy failed(&p, &AIProvider::analysisFailed);
        QSignalSpy complete(&p, &AIProvider::analysisComplete);
        QTest::ignoreMessage(QtDebugMsg, QRegularExpression("Anthropic.*model"));

        QJsonArray messages;
        QJsonObject userMsg;
        userMsg["role"] = QStringLiteral("user");
        userMsg["content"] = QStringLiteral("why is it sour?");
        messages.append(userMsg);
        p.analyzeConversation(QStringLiteral("system"), messages);

        QVERIFY(complete.wait(5000));
        const QString shown = complete.first().first().toString();
        QVERIFY2(shown.startsWith(QStringLiteral("Grind fine")), qPrintable(shown));
        QVERIFY2(shown.contains(QStringLiteral("cut off")), qPrintable(shown));
        QCOMPARE(failed.size(), 0);
    }

    // A 200 whose content holds blocks but NO text block is what #1691 looked
    // like on the wire. Guards the block-type logging that makes such a reply
    // placeable from the log rather than indistinguishable from a refusal.
    void anthropicReportsATextlessReply()
    {
        QNetworkAccessManager nam;
        FakeProviderServer server;
        server.respondWith(
            "{\"content\":[{\"type\":\"thinking\",\"thinking\":\"\"}],\"stop_reason\":\"max_tokens\"}");
        AnthropicProvider p(&nam, QStringLiteral("key"));
        p.setBaseUrl(server.baseUrl());

        QSignalSpy failed(&p, &AIProvider::analysisFailed);
        QSignalSpy complete(&p, &AIProvider::analysisComplete);
        QTest::ignoreMessage(QtDebugMsg, QRegularExpression("Anthropic.*model"));

        p.analyze(QStringLiteral("system"), QStringLiteral("user"));
        QVERIFY(failed.wait(5000));
        QCOMPARE(failed.size(), 1);
        QVERIFY(failed.first().first().toString().contains(QStringLiteral("cut off")));
        QCOMPARE(complete.size(), 0);
    }

    // stop_reason "pause_turn" means the API paused a server-tool turn and the
    // response must be fed back to continue — its text is partial by
    // definition. analyzeUrl() attaches web_fetch, so this path can reach it.
    void anthropicTreatsPauseTurnAsUnfinished()
    {
        QNetworkAccessManager nam;
        FakeProviderServer server;
        server.respondWith("{\"content\":[{\"type\":\"text\",\"text\":\"Fetching...\"}],"
                           "\"stop_reason\":\"pause_turn\"}");
        AnthropicProvider p(&nam, QStringLiteral("key"));
        p.setBaseUrl(server.baseUrl());

        QSignalSpy failed(&p, &AIProvider::analysisFailed);
        QSignalSpy complete(&p, &AIProvider::analysisComplete);
        QTest::ignoreMessage(QtDebugMsg, QRegularExpression("Anthropic.*model"));

        p.analyzeUrl(QStringLiteral("system"), QStringLiteral("https://example.com/bag"));
        QVERIFY(failed.wait(5000));
        QCOMPARE(complete.size(), 0);
    }

    // The truncation branch must not swallow good replies: stop_reason
    // "end_turn" with real text still completes.
    void anthropicCompleteReplyStillSucceeds()
    {
        QNetworkAccessManager nam;
        FakeProviderServer server;
        server.respondWith(
            "{\"content\":[{\"type\":\"text\",\"text\":\"Grind finer.\"}],\"stop_reason\":\"end_turn\"}");
        AnthropicProvider p(&nam, QStringLiteral("key"));
        p.setBaseUrl(server.baseUrl());

        QSignalSpy complete(&p, &AIProvider::analysisComplete);
        QSignalSpy failed(&p, &AIProvider::analysisFailed);
        p.analyze(QStringLiteral("system"), QStringLiteral("user"));
        QVERIFY(complete.wait(5000));
        QCOMPARE(complete.first().first().toString(), QStringLiteral("Grind finer."));
        QCOMPARE(failed.size(), 0);
    }

    // The other four providers' truncation detection, which shipped untested —
    // and on Gemini, broken: its finishReason was read AFTER an
    // `parts.isEmpty()` early return, so a candidate that stopped with nothing
    // to show never reached the check. Each predicate is a bare string compare
    // against a different vendor spelling, with no compiler or linter signal if
    // it is wrong.
    void otherProvidersReportTruncation_data()
    {
        QTest::addColumn<QString>("provider");
        QTest::addColumn<QByteArray>("truncatedBody");
        QTest::addColumn<QByteArray>("goodBody");
        QTest::addColumn<QString>("warningPrefix");

        QTest::newRow("openai-chat")
            << QStringLiteral("openai")
            << QByteArray("{\"choices\":[{\"finish_reason\":\"length\","
                          "\"message\":{\"content\":\"Grind fine\"}}]}")
            << QByteArray("{\"choices\":[{\"finish_reason\":\"stop\","
                          "\"message\":{\"content\":\"Grind finer.\"}}]}")
            << QStringLiteral("OpenAI.*model");

        // No content key at all — the shape that made this branch unreachable.
        QTest::newRow("gemini-thinking-ate-the-budget")
            << QStringLiteral("gemini")
            << QByteArray("{\"candidates\":[{\"finishReason\":\"MAX_TOKENS\"}]}")
            << QByteArray("{\"candidates\":[{\"finishReason\":\"STOP\",\"content\":"
                          "{\"parts\":[{\"text\":\"Grind finer.\"}]}}]}")
            << QStringLiteral("Gemini.*model");

        QTest::newRow("openrouter")
            << QStringLiteral("openrouter")
            << QByteArray("{\"choices\":[{\"finish_reason\":\"length\","
                          "\"message\":{\"content\":\"Grind fine\"}}]}")
            << QByteArray("{\"choices\":[{\"finish_reason\":\"stop\","
                          "\"message\":{\"content\":\"Grind finer.\"}}]}")
            << QStringLiteral("OpenRouter.*model");

        QTest::newRow("ollama")
            << QStringLiteral("ollama")
            << QByteArray("{\"done_reason\":\"length\",\"message\":{\"content\":\"Grind fine\"}}")
            << QByteArray("{\"done_reason\":\"stop\",\"message\":{\"content\":\"Grind finer.\"}}")
            << QStringLiteral("Ollama.*model");
    }

    void otherProvidersReportTruncation()
    {
        QFETCH(QString, provider);
        QFETCH(QByteArray, truncatedBody);
        QFETCH(QByteArray, goodBody);
        QFETCH(QString, warningPrefix);

        QNetworkAccessManager nam;
        FakeProviderServer server;

        // Ollama takes its endpoint as a constructor arg; the rest use setBaseUrl.
        std::unique_ptr<AIProvider> p;
        if (provider == QLatin1String("openai")) {
            auto* o = new OpenAIProvider(&nam, QStringLiteral("key"));
            o->setBaseUrl(server.baseUrl());
            p.reset(o);
        } else if (provider == QLatin1String("gemini")) {
            auto* g = new GeminiProvider(&nam, QStringLiteral("key"));
            g->setBaseUrl(server.baseUrl());
            p.reset(g);
        } else if (provider == QLatin1String("openrouter")) {
            auto* r = new OpenRouterProvider(&nam, QStringLiteral("key"));
            r->setBaseUrl(server.baseUrl());
            p.reset(r);
        } else {
            p.reset(new OllamaProvider(&nam, server.baseUrl(), QStringLiteral("llama3")));
        }

        // Truncated → failure on the machine-parsed analyze() path.
        server.respondWith(truncatedBody);
        QSignalSpy failed(p.get(), &AIProvider::analysisFailed);
        QSignalSpy complete(p.get(), &AIProvider::analysisComplete);
        QTest::ignoreMessage(QtDebugMsg, QRegularExpression(warningPrefix));
        p->analyze(QStringLiteral("system"), QStringLiteral("user"));
        QVERIFY(failed.wait(5000));
        QVERIFY2(failed.first().first().toString().contains(QStringLiteral("cut off")),
                 qPrintable(failed.first().first().toString()));
        QCOMPARE(complete.size(), 0);

        // ...and a complete reply is still emitted, so the new guard can't
        // start swallowing good answers unnoticed.
        server.respondWith(goodBody);
        p->analyze(QStringLiteral("system"), QStringLiteral("user"));
        QVERIFY(complete.wait(5000));
        QCOMPARE(complete.first().first().toString(), QStringLiteral("Grind finer."));
        QCOMPARE(failed.size(), 1);  // still just the first one

        // The model's thinking setting reaches the wire.
        const QJsonObject body = server.lastRequest();
        if (provider == QLatin1String("openai")) {
            QJsonObject expected;
            AIRequestShape::disableOpenAIReasoning(expected, p->modelName());
            QCOMPARE(body["reasoning_effort"], expected["reasoning_effort"]);
        } else if (provider == QLatin1String("gemini")) {
            QCOMPARE(body["generationConfig"].toObject(), AIRequestShape::geminiGenerationConfig(p->modelName()));
        }
    }
};

QTEST_GUILESS_MAIN(tst_AIProviders)

#include "tst_aiproviders.moc"
