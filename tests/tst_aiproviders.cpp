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
// These methods are pure (no network I/O) and public, so no mocking or
// friend-class access is needed. Gemini is covered too — it shipped the
// pattern this test also guards, previously untested.

#include <QtTest>
#include <QNetworkAccessManager>
#include <QRegularExpression>
#include <QList>
#include <QPair>
#include <QString>

#include "ai/aiprovider.h"

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
    void openAiCatalogAndSelection()
    {
        QNetworkAccessManager nam;
        checkProvider<OpenAIProvider>(nam, {
            { "gpt-5.4-mini", "GPT-5.4 mini" },
            { "gpt-5.4", "GPT-5.4" },
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
};

QTEST_GUILESS_MAIN(tst_AIProviders)

#include "tst_aiproviders.moc"
