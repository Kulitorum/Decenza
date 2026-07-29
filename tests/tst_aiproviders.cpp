// tst_aiproviders — pins the per-provider model-catalog contract that the
// AI Advisor's model picker and settings round-trip depend on.
//
// Each provider that offers a user-selectable model (OpenAI, Anthropic,
// Gemini) exposes availableModels() as the single source of truth for both
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
// friend-class access is needed. Gemini is covered too — it shipped the
// pattern this test also guards, previously untested.
//
// The suite also pins the Anthropic REQUEST SHAPE (#1691), which does need a
// canned-response server: what broke there was an absent field in the posted
// JSON, not anything a pure method exposes. See FakeAnthropicServer below.

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

#include "ai/aiprovider.h"
#include "core/translationmanager.h"

// Canned-response HTTP server for the Anthropic request/reply contract below.
// Records the request BODY (not just the request line) because what these
// tests assert is what we send in the JSON — the #1691 regression was an
// absent field, invisible from the URL. Same shape as FakeBeanBaseServer in
// tst_beanbaseclient.cpp.
//
// NOTE: no raw string literals anywhere in this file — moc miscounts the
// braces inside "..." and silently drops every class declared after one.
class FakeAnthropicServer : public QObject {
    Q_OBJECT
public:
    FakeAnthropicServer() {
        connect(&m_server, &QTcpServer::newConnection, this, [this]() {
            while (m_server.hasPendingConnections()) {
                QTcpSocket* sock = m_server.nextPendingConnection();
                auto* buf = new QByteArray;
                connect(sock, &QTcpSocket::readyRead, this, [this, sock, buf]() {
                    buf->append(sock->readAll());
                    // Wait for the whole body: a POST can arrive in several
                    // chunks, and a half-read body parses as invalid JSON.
                    const qsizetype headerEnd = buf->indexOf("\r\n\r\n");
                    if (headerEnd < 0) return;
                    const QByteArray headers = buf->left(headerEnd);
                    const qsizetype clPos = headers.indexOf("Content-Length: ");
                    if (clPos < 0) return;
                    const qsizetype expected = headers.mid(clPos + 16).split('\r').first().toLongLong();
                    const QByteArray body = buf->mid(headerEnd + 4);
                    if (body.size() < expected) return;

                    m_requestBodies.append(body);
                    const QByteArray resp =
                        "HTTP/1.1 200 OK\r\n"
                        "Content-Type: application/json\r\n"
                        "Content-Length: " + QByteArray::number(m_responseBody.size()) + "\r\n"
                        "Connection: close\r\n"
                        "\r\n" + m_responseBody;
                    sock->write(resp);
                    sock->disconnectFromHost();
                });
                connect(sock, &QTcpSocket::disconnected, sock, [sock, buf]() {
                    delete buf;
                    sock->deleteLater();
                });
            }
        });
        const bool ok = m_server.listen(QHostAddress::LocalHost, 0);
        Q_ASSERT(ok);
    }

    QString baseUrl() const {
        return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort());
    }

    void respondWith(const QByteArray& body) { m_responseBody = body; }

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
};

namespace {

using Catalog = QList<QPair<QString, QString>>;  // (id, displayName), UI order

// Exercise the full catalog + setModel guard contract for one concrete
// provider type against its expected catalog. Templated because setModel()
// is declared per-derived-class, not as a base virtual, so it can't be
// called through an AIProvider*.
template <typename ProviderT>
void checkProvider(QNetworkAccessManager& nam, const Catalog& expected)
{
    QVERIFY2(expected.size() >= 2, "test assumes a >1 entry catalog (picker only shows then)");

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

    // modelHint() is the guidance line shown under the model picker in both
    // the app and the ShotServer web page. A multi-model provider must have
    // one (both UIs gate on non-empty), and it must mention every catalog
    // entry by display name — a catalog bump that forgets the hint would ship
    // stale model-comparison advice to both UIs at once.
    const QString hint = p.modelHint();
    QVERIFY2(!hint.isEmpty(), "multi-model provider must provide a modelHint()");
    for (const AIProvider::ModelOption& opt : models) {
        QVERIFY2(hint.contains(opt.displayName),
                 qPrintable(QStringLiteral("modelHint() does not mention catalog model '%1'")
                                .arg(opt.displayName)));
    }

    // Selecting the opt-in (last) model switches the wire model and its label.
    const QString optId = expected.last().first;
    const QString optName = expected.last().second;
    p.setModel(optId);
    QCOMPARE(p.modelName(), optId);
    QCOMPARE(p.shortModelName(), optName);

    // Empty id (settings unset) is a no-op — keeps the current selection.
    p.setModel(QString());
    QCOMPARE(p.modelName(), optId);

    // Unknown id (stale/renamed stored value) warns and is ignored — never
    // clobbers the current model with a dead id that would 400 every request.
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("ignoring unknown model id"));
    p.setModel(QStringLiteral("model-that-does-not-exist"));
    QCOMPARE(p.modelName(), optId);
}

} // namespace

class tst_AIProviders : public QObject {
    Q_OBJECT

private slots:
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

    void init() { QTest::failOnWarning(); }
    void openAiCatalogAndSelection()
    {
        QNetworkAccessManager nam;
        checkProvider<OpenAIProvider>(nam, {
            { "gpt-5.4", "GPT-5.4" },
            { "gpt-5.4-mini", "GPT-5.4 mini" },
        });
    }

    void anthropicCatalogAndSelection()
    {
        QNetworkAccessManager nam;
        checkProvider<AnthropicProvider>(nam, {
            { "claude-sonnet-4-6", "Sonnet 4.6" },
            { "claude-sonnet-5", "Sonnet 5" },
        });
    }

    void geminiCatalogAndSelection()
    {
        QNetworkAccessManager nam;
        checkProvider<GeminiProvider>(nam, {
            { "gemini-2.5-flash", "2.5 Flash" },
            { "gemini-3.5-flash", "3.5 Flash" },
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
        QVERIFY(!OpenRouterProvider(&nam, "key", "model").supportsUrlAnalysis());
        QVERIFY(!OllamaProvider(&nam, "http://localhost:11434", "model").supportsUrlAnalysis());
    }

    // #1691: every Anthropic request must turn thinking OFF explicitly.
    //
    // The default is not stable across models — claude-sonnet-4-6 runs without
    // thinking when the field is omitted, claude-sonnet-5 runs adaptive. Since
    // max_tokens bounds thinking + text together, an omitted field on Sonnet 5
    // let thinking eat the whole budget and the reply carried no text block at
    // all, failing 100% of the time. Nothing in the request URL shows this, so
    // the assertion has to be on the posted JSON.
    void anthropicRequestsDisableThinkingAndUseTheRaisedCap()
    {
        QNetworkAccessManager nam;
        FakeAnthropicServer server;
        AnthropicProvider p(&nam, QStringLiteral("key"));
        p.setBaseUrl(server.baseUrl());

        QSignalSpy complete(&p, &AIProvider::analysisComplete);

        p.analyze(QStringLiteral("system"), QStringLiteral("user"));
        QVERIFY(complete.wait(5000));
        QCOMPARE(server.requestCount(), 1);

        QJsonObject body = server.lastRequest();
        QCOMPARE(body["thinking"].toObject()["type"].toString(), QStringLiteral("disabled"));
        QCOMPARE(body["max_tokens"].toInt(), 4096);

        // analyzeConversation() is the path the in-app advisor actually uses
        // (AIConversation::sendRequest) and is where #1691 was reported.
        QJsonArray messages;
        QJsonObject userMsg;
        userMsg["role"] = QStringLiteral("user");
        userMsg["content"] = QStringLiteral("how did this shot taste?");
        messages.append(userMsg);
        p.analyzeConversation(QStringLiteral("system"), messages);
        QVERIFY(complete.wait(5000));
        QCOMPARE(server.requestCount(), 2);

        body = server.lastRequest();
        QCOMPARE(body["thinking"].toObject()["type"].toString(), QStringLiteral("disabled"));
        QCOMPARE(body["max_tokens"].toInt(), 4096);
    }

    // A 200 whose content holds blocks but no TEXT block is what #1691 looked
    // like on the wire. Belt and braces for the fix above: if thinking ever
    // returns (a new model default, a future feature), the user must get the
    // truncation message and the log must name the block types — the old
    // generic "empty response content" was indistinguishable from a refusal
    // and cost three days of user reports to place.
    void anthropicReportsAThinkingOnlyReplyAsTruncated()
    {
        QNetworkAccessManager nam;
        FakeAnthropicServer server;
        server.respondWith(
            "{\"content\":[{\"type\":\"thinking\",\"thinking\":\"\"}],\"stop_reason\":\"max_tokens\"}");
        AnthropicProvider p(&nam, QStringLiteral("key"));
        p.setBaseUrl(server.baseUrl());

        QSignalSpy failed(&p, &AIProvider::analysisFailed);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Anthropic: stop_reason"));

        p.analyze(QStringLiteral("system"), QStringLiteral("user"));
        QVERIFY(failed.wait(5000));
        QCOMPARE(failed.size(), 1);
        const QString message = failed.first().first().toString();
        QVERIFY2(message.contains(QStringLiteral("cut off")),
                 qPrintable(QStringLiteral("expected the truncation message, got: ") + message));
    }

    // The truncation branch must not swallow good replies: stop_reason
    // "end_turn" with real text still completes.
    void anthropicCompleteReplyStillSucceeds()
    {
        QNetworkAccessManager nam;
        FakeAnthropicServer server;
        server.respondWith(
            "{\"content\":[{\"type\":\"text\",\"text\":\"Grind finer.\"}],\"stop_reason\":\"end_turn\"}");
        AnthropicProvider p(&nam, QStringLiteral("key"));
        p.setBaseUrl(server.baseUrl());

        QSignalSpy complete(&p, &AIProvider::analysisComplete);
        p.analyze(QStringLiteral("system"), QStringLiteral("user"));
        QVERIFY(complete.wait(5000));
        QCOMPARE(complete.first().first().toString(), QStringLiteral("Grind finer."));
    }
};

QTEST_GUILESS_MAIN(tst_AIProviders)

#include "tst_aiproviders.moc"
