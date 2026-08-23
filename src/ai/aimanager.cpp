#include "aimanager.h"
#include "core/appsettings.h"
#include "aiprovider.h"
#include "aiconversation.h"
#include "shotsummarizer.h"
#include "../core/settings.h"
#include "../core/settings_ai.h"
#include "../core/grinderaliases.h"
#include "../controllers/profilemanager.h"
#include "dialing_blocks.h"
#include "../models/shotdatamodel.h"
#include "../profile/profile.h"
#include "../network/visualizeruploader.h"
#include "../history/shothistorystorage.h"
#include "../core/translationmanager.h"

#include <QNetworkAccessManager>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QDateTime>
#include <QLocale>
#include <QDebug>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QThread>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include "../core/dbutils.h"
#include <QPointer>
#include <QCoreApplication>
#include <QRegularExpression>
#include <cmath>

namespace {
// Coerce a QML-supplied shot argument into a ShotProjection. QML hands the
// shot-taking Q_INVOKABLEs below one of two shapes:
//   - a real ShotProjection (fresh shot via onShotReady, or History reload) —
//     arrives as a QVariant wrapping ShotProjection;
//   - a plain JS object — the optimistic edit clone built by
//     PostShotReviewPage.clonePersistedShot() after an in-place edit — arrives
//     as a QVariant wrapping QVariantMap.
// A JS object does NOT auto-convert to a `const ShotProjection&` parameter on
// the QML→C++ argument-binding path in Qt 6.11 (it throws "Could not convert
// argument 0 from [object Object] to ShotProjection"), which is why the AI
// Advice / Discuss buttons died after any edit (#1298). Accepting QVariant and
// coercing explicitly here handles both shapes. fromVariantMap reconstructs a
// full ShotProjection — clonePersistedShot copies every field, including the
// curve arrays, so the summarizer gets complete data.
ShotProjection coerceShot(const QVariant& v)
{
    if (v.userType() == qMetaTypeId<ShotProjection>())
        return v.value<ShotProjection>();
    const QVariantMap m = v.toMap();
    // An empty map means QML passed null/undefined or a non-map scalar — the
    // result would be a default ShotProjection (durationSec=0), which
    // isMistakeShot reads as a mistake and silently suppresses the AI summary.
    // That's a benign degrade, but log it so a future QML arg-shape regression
    // is debuggable instead of silently flagging every shot as a mistake.
    if (m.isEmpty())
        qWarning() << "AIManager::coerceShot: empty/non-map shot arg (type"
                   << v.typeName() << ") — shot will read as a mistake";
    return ShotProjection::fromVariantMap(m);
}
}

AIManager::AIManager(QNetworkAccessManager* networkManager, Settings* settings, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_networkManager(networkManager)
    , m_summarizer(std::make_unique<ShotSummarizer>(this))
{
    Q_ASSERT(networkManager);
    createProviders();

    // Create conversation handler for multi-turn interactions
    m_conversation = new AIConversation(this, this);

    // One-time clear: grinder calibration (v1.7.2) changed what the advisor
    // knows per shot; old conversations anchored on "I don't have LRV3 data"
    // are misleading. Fires once per device, then becomes a no-op.
    clearAllConversationsOnce(QStringLiteral("grinder_calibration_v1.7.2"));
    // Pre-change threads are keyed without the equipment package and their turns
    // carry cross-equipment context verbatim, so changing the key already orphans
    // them; and their user messages are the old prose-wrapped shape, which nothing
    // reads any more — one JSON object per turn is the only shape now.
    //
    // v2, not v1: the marker is per id and v1 has already fired on any device that
    // ran an interim build of this change. Those devices would keep prose turns
    // that no reader can render.
    clearAllConversationsOnce(QStringLiteral("equipment_scoped_conversations_v2"));

    // Migrate legacy single-conversation storage if needed
    migrateFromLegacyConversation();

    // Load conversation index and restore most recent conversation
    loadConversationIndex();
    loadMostRecentConversation();

    // Connect to settings changes
    connect(m_settings->ai(), &SettingsAI::configurationChanged, this, &AIManager::onSettingsChanged);
}

AIManager::~AIManager() = default;

void AIManager::createProviders()
{
    // Create OpenAI provider
    QString openaiKey = m_settings->ai()->openaiApiKey();
    auto* openai = new OpenAIProvider(m_networkManager, openaiKey, this);
    openai->setModel(m_settings->ai()->providerModel("openai"));  // empty → keeps default
    openai->setBaseUrl(m_settings->ai()->openaiEndpoint());
    connect(openai, &AIProvider::analysisComplete, this, &AIManager::onAnalysisComplete);
    connect(openai, &AIProvider::analysisFailed, this, &AIManager::onAnalysisFailed);
    connect(openai, &AIProvider::testResult, this, &AIManager::onTestResult);
    m_openaiProvider.reset(openai);

    // Create Anthropic provider
    QString anthropicKey = m_settings->ai()->anthropicApiKey();
    auto* anthropic = new AnthropicProvider(m_networkManager, anthropicKey, this);
    anthropic->setModel(m_settings->ai()->providerModel("anthropic"));  // empty → keeps default
    anthropic->setBaseUrl(m_settings->ai()->anthropicEndpoint());
    connect(anthropic, &AIProvider::analysisComplete, this, &AIManager::onAnalysisComplete);
    connect(anthropic, &AIProvider::analysisFailed, this, &AIManager::onAnalysisFailed);
    connect(anthropic, &AIProvider::testResult, this, &AIManager::onTestResult);
    m_anthropicProvider.reset(anthropic);

    // Create Gemini provider
    QString geminiKey = m_settings->ai()->geminiApiKey();
    auto* gemini = new GeminiProvider(m_networkManager, geminiKey, this);
    gemini->setModel(m_settings->ai()->providerModel("gemini"));  // empty → keeps default
    connect(gemini, &AIProvider::analysisComplete, this, &AIManager::onAnalysisComplete);
    connect(gemini, &AIProvider::analysisFailed, this, &AIManager::onAnalysisFailed);
    connect(gemini, &AIProvider::testResult, this, &AIManager::onTestResult);
    m_geminiProvider.reset(gemini);

    // Create OpenRouter provider
    QString openrouterKey = m_settings->ai()->openrouterApiKey();
    QString openrouterModel = m_settings->ai()->openrouterModel();
    auto* openrouter = new OpenRouterProvider(m_networkManager, openrouterKey, openrouterModel, this);
    connect(openrouter, &AIProvider::analysisComplete, this, &AIManager::onAnalysisComplete);
    connect(openrouter, &AIProvider::analysisFailed, this, &AIManager::onAnalysisFailed);
    connect(openrouter, &AIProvider::testResult, this, &AIManager::onTestResult);
    m_openrouterProvider.reset(openrouter);

    // Create Ollama provider
    QString ollamaEndpoint = m_settings->ai()->ollamaEndpoint();
    QString ollamaModel = m_settings->ai()->ollamaModel();
    auto* ollama = new OllamaProvider(m_networkManager, ollamaEndpoint, ollamaModel, this);
    connect(ollama, &AIProvider::analysisComplete, this, &AIManager::onAnalysisComplete);
    connect(ollama, &AIProvider::analysisFailed, this, &AIManager::onAnalysisFailed);
    connect(ollama, &AIProvider::testResult, this, &AIManager::onTestResult);
    connect(ollama, &OllamaProvider::modelsRefreshed, this, &AIManager::onOllamaModelsRefreshed);
    m_ollamaProvider.reset(ollama);
}

void AIManager::setTranslationManager(TranslationManager* tm)
{
    m_translationManager = tm;
    // Fan out to every owned provider (created in the ctor, before this runs)
    // and the conversation, so their user-visible error strings localize too.
    for (AIProvider* p : { m_openaiProvider.get(), m_anthropicProvider.get(),
                           m_geminiProvider.get(), m_openrouterProvider.get(),
                           m_ollamaProvider.get() }) {
        if (p) p->setTranslationManager(tm);
    }
    if (m_conversation) m_conversation->setTranslationManager(tm);
}

QString AIManager::tr_(const char* key, const char* fallback) const
{
    if (m_translationManager)
        return m_translationManager->translateString(QString::fromUtf8(key),
                                               QString::fromUtf8(fallback));
    return QString::fromUtf8(fallback);
}

QString AIManager::selectedProvider() const
{
    return m_settings->ai()->aiProvider();
}

QString AIManager::currentModelName() const
{
    AIProvider* provider = currentProvider();
    return provider ? provider->modelName() : QString();
}

QString AIManager::modelDisplayName(const QString& providerId) const
{
    AIProvider* provider = providerById(providerId);
    return provider ? provider->shortModelName() : QString();
}

QVariantList AIManager::availableModels(const QString& providerId) const
{
    QVariantList out;
    AIProvider* provider = providerById(providerId);
    if (!provider) return out;
    const QList<AIProvider::ModelOption> models = provider->availableModels();
    for (const AIProvider::ModelOption& opt : models) {
        // Keys are a contract with SettingsAITab.qml and the ShotServer
        // settings page JS (served as providerModelCatalogs): "name" is the
        // display label, "id" is read back on selection. Keep both in sync
        // with those consumers.
        QVariantMap entry;
        entry["id"] = opt.id;
        entry["name"] = opt.displayName;
        out.append(entry);
    }
    return out;
}

QString AIManager::modelHint(const QString& providerId) const
{
    AIProvider* provider = providerById(providerId);
    return provider ? provider->modelHint() : QString();
}

QString AIManager::costHint(const QString& providerId, const QString& modelId) const
{
    AIProvider* provider = providerById(providerId);
    if (!provider)
        return {};
    return modelId.isEmpty() ? provider->costHint() : provider->costHintFor(modelId);
}

void AIManager::setSelectedProvider(const QString& provider)
{
    if (selectedProvider() != provider) {
        m_settings->ai()->setAiProvider(provider);
        emit providerChanged();
        emit configurationChanged();
    }
}

QStringList AIManager::availableProviders() const
{
    return {"openai", "anthropic", "gemini", "openrouter", "ollama"};
}

bool AIManager::isConfigured() const
{
    AIProvider* provider = currentProvider();
    return provider && provider->isConfigured();
}

AIProvider* AIManager::providerById(const QString& providerId) const
{
    if (providerId == "openai") return m_openaiProvider.get();
    if (providerId == "anthropic") return m_anthropicProvider.get();
    if (providerId == "gemini") return m_geminiProvider.get();
    if (providerId == "openrouter") return m_openrouterProvider.get();
    if (providerId == "ollama") return m_ollamaProvider.get();
    return nullptr;
}

AIProvider* AIManager::currentProvider() const
{
    AIProvider* provider = providerById(selectedProvider());
    return provider ? provider : m_openaiProvider.get();  // Default
}

std::optional<QJsonObject> AIManager::parseStructuredNext(const QString& assistantMessage)
{
    // Locate the LAST fenced ```json ... ``` block whose closing fence is
    // the final non-whitespace content in the message. Mid-message blocks
    // (e.g., the model echoing a snippet from the user) MUST be ignored —
    // the recommendation block always trails the prose.
    //
    // Strategy: walk all ``` fence positions, pair them as opener/closer,
    // and check the LAST pair. If that pair's opener is tagged `json`
    // (case-insensitive) and its closer is followed only by whitespace,
    // parse the inner body. Anything else → std::nullopt.
    if (assistantMessage.isEmpty()) return std::nullopt;

    QList<qsizetype> fenceStarts;
    fenceStarts.reserve(8);
    qsizetype searchFrom = 0;
    while (true) {
        const qsizetype pos = assistantMessage.indexOf(QStringLiteral("```"), searchFrom);
        if (pos < 0) break;
        fenceStarts.append(pos);
        searchFrom = pos + 3;
    }
    if (fenceStarts.size() < 2) return std::nullopt;

    // Take the last two fences unconditionally — odd total counts (a
    // stray ``` somewhere earlier in the prose) MUST NOT silently drop a
    // structurally valid trailing block. The closer-followed-only-by-
    // whitespace check below is what actually enforces "this is the
    // trailing block."
    const qsizetype openerStart = fenceStarts.at(fenceStarts.size() - 2);
    const qsizetype closerStart = fenceStarts.at(fenceStarts.size() - 1);

    // Closer must be followed only by whitespace.
    const qsizetype closerEnd = closerStart + 3;
    for (qsizetype i = closerEnd; i < assistantMessage.size(); ++i) {
        if (!assistantMessage[i].isSpace()) return std::nullopt;
    }

    // Tag: characters between opener fence and the next newline.
    const qsizetype tagStart = openerStart + 3;
    const qsizetype newlineAfterOpener = assistantMessage.indexOf(QLatin1Char('\n'), tagStart);
    if (newlineAfterOpener < 0 || newlineAfterOpener >= closerStart) return std::nullopt;
    const QString tag = assistantMessage.mid(tagStart, newlineAfterOpener - tagStart).trimmed();
    if (tag.compare(QStringLiteral("json"), Qt::CaseInsensitive) != 0) return std::nullopt;

    const QString inner = assistantMessage.mid(newlineAfterOpener + 1, closerStart - newlineAfterOpener - 1).trimmed();
    if (inner.isEmpty()) return std::nullopt;

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(inner.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError) {
        qWarning() << "AIManager::parseStructuredNext: structuredNext parse failed —" << err.errorString();
        return std::nullopt;
    }
    if (!doc.isObject()) return std::nullopt;
    return doc.object();
}

QJsonArray AIManager::sanitizeApiMessages(const QJsonArray& messages)
{
    // AIConversation stores each turn as {role, content[, shotId][, structuredNext]}.
    // shotId (issue #1053 shot latching) and structuredNext are Decenza-internal
    // bookkeeping and must never reach a provider. The Anthropic Messages API
    // rejects unknown per-message keys with HTTP 400 ("messages.N.shotId: Extra
    // inputs are not permitted"), which killed every dial-in conversation request.
    // Whitelist role + content only; content is copied verbatim (a plain string
    // here — providers do their own cache-block wrapping downstream).
    QJsonArray out;
    for (const QJsonValue& v : messages) {
        const QJsonObject msg = v.toObject();
        QJsonObject clean;
        clean["role"] = msg.value("role");
        clean["content"] = msg.value("content");
        out.append(clean);
    }
    return out;
}

// Heuristic for "the prior assistant message asked the user about
// taste". Conservative: a false negative just means we don't auto-
// persist — the user can still rate via the editor or the rating slider.
// False positives are the dangerous mode (a recommendation reply
// mentioning a "score from 75" past tense triggers a writeback from
// the user's next prose number). To minimize false positives:
//   1. The marker list is tight — bare "score" / "how did" are too
//      common in advisor recommendation prose. Require taste-specific
//      phrasings.
//   2. The message must end in a question — the assistant has to
//      actually be asking, not just discussing scores in passing.
static bool priorAssistantAskedAboutTaste(const QString& priorAssistantMessage)
{
    if (priorAssistantMessage.isEmpty()) return false;
    // Must end in a question mark (allow trailing whitespace and the
    // structuredNext fenced block from #1054).
    QString trimmed = priorAssistantMessage.trimmed();
    if (trimmed.endsWith(QStringLiteral("```"))) {
        // Strip the trailing fenced JSON block before testing for a
        // question-mark suffix on the prose body.
        const qsizetype openerStart = trimmed.lastIndexOf(QStringLiteral("```"),
            trimmed.length() - 4);
        if (openerStart > 0) trimmed = trimmed.left(openerStart).trimmed();
    }
    if (!trimmed.endsWith(QLatin1Char('?'))) return false;

    const QString lc = priorAssistantMessage.toLower();
    static const QStringList markers{
        QStringLiteral("how did it taste"),
        QStringLiteral("how did this taste"),
        QStringLiteral("how did this shot taste"),
        QStringLiteral("how does it taste"),
        QStringLiteral("how does this taste"),
        QStringLiteral("how would you rate"),
        QStringLiteral("rate this shot"),
        QStringLiteral("rate the shot"),
        QStringLiteral("score from 1"),
        QStringLiteral("score 1-100"),
        QStringLiteral("score 1 to 100"),
        QStringLiteral("tasting notes"),
        QStringLiteral("what did you think of the taste"),
    };
    for (const QString& m : markers) {
        if (lc.contains(m)) return true;
    }
    return false;
}

void AIManager::maybePersistRatingFromReply(const QString& userReply,
                                             const QString& priorAssistantMessage,
                                             qint64 shotId)
{
    if (shotId <= 0) return;
    if (!m_shotHistory) return;
    if (!priorAssistantAskedAboutTaste(priorAssistantMessage)) return;

    const auto parsed = parseUserRatingReply(userReply);
    if (!parsed.has_value()) return;

    QVariantMap metadata;
    metadata.insert(QStringLiteral("enjoyment"), parsed->score);
    if (!parsed->notes.isEmpty()) {
        metadata.insert(QStringLiteral("espressoNotes"), parsed->notes);
    }
    qDebug() << "AIManager: conversational rating capture — writing"
             << parsed->score << "to shot" << shotId
             << "(notes" << (parsed->notes.isEmpty() ? "absent" : "present") << ")";
    // Same reason as the bean-metadata capture below: this is the user's own
    // rating, so a write that lands nowhere must not pass unnoticed.
    m_pendingMetadataWrites[shotId]++;
    m_shotHistory->requestUpdateShotMetadata(shotId, metadata);
}

// shot-metadata-capture: did the prior assistant turn ask the user about
// beans (roast level, brand, type, date)? Mirrors priorAssistantAskedAboutTaste
// — message must end in a question mark (allow trailing structuredNext fence)
// and contain at least one bean-asking marker.
static bool priorAssistantAskedAboutBean(const QString& priorAssistantMessage)
{
    if (priorAssistantMessage.isEmpty()) return false;
    QString trimmed = priorAssistantMessage.trimmed();
    if (trimmed.endsWith(QStringLiteral("```"))) {
        const qsizetype openerStart = trimmed.lastIndexOf(QStringLiteral("```"),
            trimmed.length() - 4);
        if (openerStart > 0) trimmed = trimmed.left(openerStart).trimmed();
    }
    if (!trimmed.endsWith(QLatin1Char('?'))) return false;

    const QString lc = priorAssistantMessage.toLower();
    static const QStringList markers{
        QStringLiteral("roast level"),
        QStringLiteral("how dark"),
        QStringLiteral("how light"),
        QStringLiteral("light or dark"),
        QStringLiteral("light, medium"),
        QStringLiteral("medium or dark"),
        QStringLiteral("what kind of bean"),
        QStringLiteral("what bean"),
        QStringLiteral("which bean"),
        QStringLiteral("describe the bean"),
        QStringLiteral("tell me about the bean"),
        QStringLiteral("what roaster"),
        QStringLiteral("which roaster"),
        QStringLiteral("when was it roasted"),
        QStringLiteral("when were they roasted"),
        QStringLiteral("roast date"),
    };
    for (const QString& m : markers) {
        if (lc.contains(m)) return true;
    }
    return false;
}

// Does the user's reply carry an explicit corrective phrasing? This is the
// SECOND gate (alongside priorAssistantAskedAboutBean) — either the model
// asked about beans OR the user volunteered a correction. Markers are
// deliberately conservative: only phrasings that strongly imply a metadata
// correction qualify ("actually...", "the coffee/bean/roast is/was...",
// "the roaster is...", "roasted on/<ISO date>"). Common conversational
// openers like "this is a great shot" or "it's really good" do NOT
// qualify — those would create false-positive writes when paired with the
// parser's roast-level patterns.
static bool userReplyVolunteersBeanCorrection(const QString& reply)
{
    const QString lc = reply.toLower();
    static const QStringList markers{
        QStringLiteral("actually it"),
        QStringLiteral("actually this"),
        QStringLiteral("actually the"),
        QStringLiteral("actually, it"),
        QStringLiteral("actually, this"),
        QStringLiteral("actually, the"),
        QStringLiteral("the coffee is"),
        QStringLiteral("the coffee was"),
        QStringLiteral("the bean is"),
        QStringLiteral("the bean was"),
        QStringLiteral("the beans are"),
        QStringLiteral("the beans were"),
        QStringLiteral("the roast is"),
        QStringLiteral("the roast was"),
        QStringLiteral("the roaster is"),
        QStringLiteral("the roaster was"),
        QStringLiteral("roasted on "),
    };
    for (const QString& m : markers) {
        if (lc.contains(m)) return true;
    }
    // "roasted YYYY-MM-DD" without "on" — match a digit immediately
    // following "roasted ". Tighter than a bare "roasted " contains check
    // (which would fire on "roasted chocolate notes").
    static const QRegularExpression rxRoastedDate(
        QStringLiteral("roasted\\s+\\d{4}-\\d{2}-\\d{2}"),
        QRegularExpression::CaseInsensitiveOption);
    return rxRoastedDate.match(reply).hasMatch();
}

// Canonicalise a free-form roast-level token into the app's stored form.
// Returns an empty string when the token doesn't match any known level.
static QString canonicalRoastLevel(const QString& raw)
{
    QString collapsed = raw.toLower().trimmed();
    collapsed.replace(QRegularExpression(QStringLiteral("[\\s\\-]+")), QString());
    if (collapsed == QLatin1String("light")) return QStringLiteral("Light");
    if (collapsed == QLatin1String("mediumlight") || collapsed == QLatin1String("lightmedium"))
        return QStringLiteral("Medium-Light");
    if (collapsed == QLatin1String("medium")) return QStringLiteral("Medium");
    if (collapsed == QLatin1String("mediumdark") || collapsed == QLatin1String("darkmedium"))
        return QStringLiteral("Medium-Dark");
    if (collapsed == QLatin1String("dark")) return QStringLiteral("Dark");
    return QString();
}

std::optional<AIManager::BeanCorrection>
AIManager::parseBeanCorrectionsFromReply(const QString& reply)
{
    if (reply.trimmed().isEmpty()) return std::nullopt;

    BeanCorrection out;

    // --- roastLevel ---------------------------------------------------------
    // Require a context word that binds the adjective to roast level so
    // tasting phrases ("dark chocolate notes", "light citrus", "this is a
    // dark crema") don't fire the parser. Two regexes; the loosest branch
    // ("(this|it|that) is a X") is split out and requires a mandatory
    // "roast" suffix so "this is a dark crema" is rejected while "this is
    // a dark roast" matches.
    //
    // Patterns covered:
    //   - "the (coffee|bean|roast|roast level) is X (roast)?"
    //   - "actually it's/this is/the X is X (roast)?"
    //   - "roast level is X"
    //   - "(this|it|that) is/was a X roast"  ← roast suffix MANDATORY
    static const QRegularExpression rxRoastStrong(
        QStringLiteral(
            // Group 1 = the level adjective (incl. medium-light / medium-dark)
            "(?:"
              "the\\s+(?:coffee|bean|beans|roast|roastlevel|roast\\s+level)\\s+(?:is|was|are|were)\\s+"
              "(?:a\\s+|really\\s+|very\\s+|pretty\\s+|quite\\s+){0,2}"
            "|"
              "actually[,\\s]+(?:it'?s|this\\s+is|the\\s+coffee\\s+is|the\\s+bean\\s+is|the\\s+roast\\s+is)\\s+"
              "(?:a\\s+|really\\s+|very\\s+|pretty\\s+|quite\\s+){0,2}"
            "|"
              "roast\\s+level\\s+is\\s+"
            ")"
            "(light|medium[\\s\\-]?light|light[\\s\\-]?medium|medium[\\s\\-]?dark|dark[\\s\\-]?medium|medium|dark)"
            "(?:\\s+roast)?\\b"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression rxRoastLooseRequiresRoastSuffix(
        QStringLiteral(
            "(?:this|it|it's|its|that)\\s+(?:is|was)\\s+"
            "(?:a\\s+|really\\s+|very\\s+|pretty\\s+|quite\\s+){1,3}"
            "(light|medium[\\s\\-]?light|light[\\s\\-]?medium|medium[\\s\\-]?dark|dark[\\s\\-]?medium|medium|dark)"
            "\\s+roast\\b"),
        QRegularExpression::CaseInsensitiveOption);
    {
        QRegularExpressionMatch m = rxRoastStrong.match(reply);
        if (!m.hasMatch()) m = rxRoastLooseRequiresRoastSuffix.match(reply);
        if (m.hasMatch()) {
            const QString canon = canonicalRoastLevel(m.captured(1));
            if (!canon.isEmpty()) out.roastLevel = canon;
        }
    }

    // --- beanBrand ----------------------------------------------------------
    // Patterns:
    //   "from <Brand>" preceded by a corrective lead-in (actually / it's /
    //     this is / the coffee is / the bean is)
    //   "the (roaster|brand) is <Brand>"
    // Brand capture is bounded to 1-4 word tokens (each starting with a
    // word character) so prose replies like "the roaster is having problems
    // with the new burr today" don't get captured as a brand. The captured
    // brand must also begin with an UPPERCASE letter — brand names in
    // conversational English are essentially always capitalised, and the
    // uppercase check rejects sentences that begin with lowercase verbs
    // ("having", "starting", "working") even when they happen to fit the
    // word-count window.
    static const QRegularExpression rxBrand(
        QStringLiteral(
            "(?:"
              "(?:actually[,\\s]+)?(?:it'?s|this\\s+is|the\\s+coffee\\s+is|the\\s+bean\\s+is|the\\s+beans\\s+are)\\s+from\\s+"
            "|"
              "the\\s+(?:roaster|brand)\\s+(?:is|was)\\s+"
            ")"
            "(\\w[\\w&'.\\-]{0,30}(?:\\s+\\w[\\w&'.\\-]{0,30}){0,3})"),
        QRegularExpression::CaseInsensitiveOption);
    {
        const QRegularExpressionMatch m = rxBrand.match(reply);
        if (m.hasMatch()) {
            QString brand = m.captured(1).trimmed();
            // Strip a trailing " roast" / " coffee" suffix the regex may have
            // grabbed on its way to the natural terminator.
            static const QRegularExpression suffix(
                QStringLiteral("\\s+(?:roast|coffee|beans?|espresso)\\s*$"),
                QRegularExpression::CaseInsensitiveOption);
            brand.replace(suffix, QString());
            // Require Title Case on the first character — rejects prose
            // continuations ("having problems...") that the lead-in would
            // otherwise gate through.
            if (!brand.isEmpty() && brand.at(0).isUpper()) out.beanBrand = brand;
        }
    }

    // --- roastDate ----------------------------------------------------------
    // Patterns:
    //   "roasted (on)? YYYY-MM-DD"
    //   "roasted (on)? <Month> <Day>(,? YYYY)?"
    // ISO form takes precedence. Natural-language form defaults year to the
    // current year when omitted.
    static const QRegularExpression rxIso(
        QStringLiteral("roasted\\s+(?:on\\s+)?(\\d{4}-\\d{2}-\\d{2})"),
        QRegularExpression::CaseInsensitiveOption);
    {
        const QRegularExpressionMatch m = rxIso.match(reply);
        if (m.hasMatch()) {
            const QDate d = QDate::fromString(m.captured(1), QStringLiteral("yyyy-MM-dd"));
            if (d.isValid()) out.roastDate = d.toString(QStringLiteral("yyyy-MM-dd"));
        }
    }
    if (!out.roastDate) {
        static const QRegularExpression rxNatural(
            QStringLiteral(
                "roasted\\s+(?:on\\s+)?"
                "(january|february|march|april|may|june|july|august|september|october|november|december|"
                 "jan|feb|mar|apr|jun|jul|aug|sep|sept|oct|nov|dec)\\s+"
                "(\\d{1,2})"
                "(?:[,\\s]+(\\d{4}))?"),
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch m = rxNatural.match(reply);
        if (m.hasMatch()) {
            const QString monthRaw = m.captured(1).toLower();
            const int day = m.captured(2).toInt();
            int year = m.captured(3).toInt();
            if (year < 1900) year = QDate::currentDate().year();
            static const QHash<QString, int> monthMap{
                {QStringLiteral("january"), 1}, {QStringLiteral("jan"), 1},
                {QStringLiteral("february"), 2}, {QStringLiteral("feb"), 2},
                {QStringLiteral("march"), 3}, {QStringLiteral("mar"), 3},
                {QStringLiteral("april"), 4}, {QStringLiteral("apr"), 4},
                {QStringLiteral("may"), 5},
                {QStringLiteral("june"), 6}, {QStringLiteral("jun"), 6},
                {QStringLiteral("july"), 7}, {QStringLiteral("jul"), 7},
                {QStringLiteral("august"), 8}, {QStringLiteral("aug"), 8},
                {QStringLiteral("september"), 9}, {QStringLiteral("sep"), 9},
                {QStringLiteral("sept"), 9},
                {QStringLiteral("october"), 10}, {QStringLiteral("oct"), 10},
                {QStringLiteral("november"), 11}, {QStringLiteral("nov"), 11},
                {QStringLiteral("december"), 12}, {QStringLiteral("dec"), 12},
            };
            const int month = monthMap.value(monthRaw, 0);
            if (month > 0) {
                const QDate d(year, month, day);
                if (d.isValid()) out.roastDate = d.toString(QStringLiteral("yyyy-MM-dd"));
            }
        }
    }

    if (out.isEmpty()) return std::nullopt;
    return out;
}

void AIManager::maybePersistBeanCorrectionFromReply(const QString& userReply,
                                                     const QString& priorAssistantMessage,
                                                     qint64 shotId)
{
    if (shotId <= 0) return;
    if (!m_shotHistory) return;

    const auto parsed = parseBeanCorrectionsFromReply(userReply);
    if (!parsed.has_value()) return;

    // Two-gate write: either the model asked about beans last turn OR the
    // user volunteered the correction with explicit corrective phrasing.
    const bool gated = priorAssistantAskedAboutBean(priorAssistantMessage)
                    || userReplyVolunteersBeanCorrection(userReply);
    if (!gated) return;

    QVariantMap metadata;
    if (parsed->roastLevel) metadata.insert(QStringLiteral("roastLevel"), *parsed->roastLevel);
    if (parsed->beanBrand)  metadata.insert(QStringLiteral("beanBrand"),  *parsed->beanBrand);
    if (parsed->roastDate)  metadata.insert(QStringLiteral("roastDate"),  *parsed->roastDate);
    if (metadata.isEmpty()) return;

    qDebug() << "AIManager: conversational bean-metadata capture — writing"
             << metadata.keys() << "to shot" << shotId;
    // Tracked so the outcome is read. This write carries something the USER
    // just said; losing it silently is the defect this replaces.
    m_pendingMetadataWrites[shotId]++;
    m_shotHistory->requestUpdateShotMetadata(shotId, metadata);
}

std::optional<AIManager::UserRatingReply> AIManager::parseUserRatingReply(const QString& reply)
{
    // A number 1-100 in the user's reply counts as a score ONLY when
    // one of these strong signals is present:
    //   (a) the number is followed by a `/100`, `out of 100`, or `%`
    //       suffix (unambiguous score notation), OR
    //   (b) the number is the first non-whitespace token in the reply
    //       (the user's reply leads with a score, optionally followed
    //       by notes — the conversational pattern "82, balanced and
    //       sweet" or "82").
    // Numbers that appear elsewhere in prose ("I dosed 18 grams",
    // "30-day-old roast") MUST NOT be picked up as ratings. Out-of-range
    // numbers, negatives, and non-numeric replies all return nullopt.
    if (reply.trimmed().isEmpty()) return std::nullopt;

    static const QRegularExpression rx(QStringLiteral(
        "(\\d+(?:\\.\\d+)?)\\s*"
        "(/\\s*100|out\\s*of\\s*100|%)?"),
        QRegularExpression::CaseInsensitiveOption);

    // Where does the first non-whitespace character sit? The "leading
    // token" gate compares each match's start against this anchor.
    qsizetype firstNonWs = 0;
    while (firstNonWs < reply.size() && reply.at(firstNonWs).isSpace()) ++firstNonWs;

    QRegularExpressionMatchIterator it = rx.globalMatch(reply);
    while (it.hasNext()) {
        QRegularExpressionMatch m = it.next();
        bool ok = false;
        const double raw = m.captured(1).toDouble(&ok);
        if (!ok) continue;
        const int rounded = static_cast<int>(std::round(raw));
        if (rounded < 1 || rounded > 100) continue;

        // Reject negatives: the regex captures digits without the minus,
        // but if the preceding character is `-` or U+2212 the user wrote
        // a negative.
        const qsizetype matchStartCheck = m.capturedStart(1);
        if (matchStartCheck > 0) {
            const QChar prev = reply.at(matchStartCheck - 1);
            if (prev == QLatin1Char('-') || prev == QChar(0x2212)) continue;
        }

        const bool hasSuffix = !m.captured(2).isEmpty();
        const bool isLeadingToken = m.capturedStart(0) == firstNonWs;
        if (!hasSuffix && !isLeadingToken) continue;  // weak anchor — skip

        UserRatingReply out;
        out.score = rounded;
        const qsizetype matchStart = m.capturedStart(0);
        const qsizetype matchEnd = m.capturedEnd(0);
        QString remaining = reply.left(matchStart) + reply.mid(matchEnd);
        static const QRegularExpression edgeTrim(QStringLiteral(
            "^[\\s,;:\\-—.!?]+|[\\s,;:\\-—.!?]+$"));
        remaining.replace(edgeTrim, QString());
        out.notes = remaining.trimmed();
        return out;
    }
    return std::nullopt;
}

QJsonObject AIManager::buildUserPromptObjectForShot(const ShotProjection& shotData)
{
    ShotSummary summary = m_summarizer->summarizeFromHistory(shotData);
    return m_summarizer->buildUserPromptObject(summary);
}

QString AIManager::buildShotAnalysisProseForShot(const QVariant& shotVariant)
{
    const ShotProjection shotData = coerceShot(shotVariant);
    ShotSummary summary = m_summarizer->summarizeFromHistory(shotData);
    return m_summarizer->buildShotAnalysisProse(summary);
}

void AIManager::enrichUserPromptObject(QJsonObject& payload,
                                       const ShotProjection& shotData,
                                       const DialingBlocks::AdvisorContextBlocks& blocks) const
{
    if (!blocks.dialInSessions.isEmpty())
        payload["dialInSessions"] = blocks.dialInSessions;
    // Mutually exclusive with dialInSessions by construction — the builder only
    // fills this one when the history query ran and matched nothing.
    if (!blocks.noDialInHistory.isEmpty())
        payload["noDialInHistory"] = blocks.noDialInHistory;
    if (!blocks.bestRecentShot.isEmpty())
        payload["bestRecentShot"] = blocks.bestRecentShot;
    if (!blocks.grinderContext.isEmpty())
        payload["grinderContext"] = blocks.grinderContext;
    if (!blocks.grinderCalibration.isEmpty())
        payload["grinderCalibration"] = blocks.grinderCalibration;
    // Closed-loop coaching: prior advisor turns paired with the user's
    // actual next shots (issue #1053). Empty array (no qualifying turns
    // yet) → key omitted; never `recentAdvice: []` placeholder.
    if (!blocks.recentAdvice.isEmpty())
        payload["recentAdvice"] = blocks.recentAdvice;
    if (shotData.isValid()) {
        const QJsonObject sawPrediction = DialingBlocks::buildSawPredictionBlock(
            m_settings, m_profileManager, shotData);
        if (!sawPrediction.isEmpty())
            payload["sawPrediction"] = sawPrediction;
    }
}

void AIManager::setShotHistoryStorage(ShotHistoryStorage* storage)
{
    if (m_shotHistory == storage) return;
    if (m_shotHistory)
        disconnect(m_shotHistory, &ShotHistoryStorage::shotMetadataUpdated, this, nullptr);

    m_shotHistory = storage;

    // The manager loads the most recent conversation in its own constructor,
    // which runs BEFORE MainController wires the storage — so that conversation
    // reached repairStaleTurnShotIds with nothing to check against and was left
    // holding whatever stale ids it had. It is the conversation the user is most
    // likely to continue, so the repair has to catch up as soon as we can check.
    if (m_shotHistory && m_conversation)
        m_conversation->repairStaleTurnShotIds();

    // Watch the outcome of OUR OWN metadata writes. The storage layer already
    // reported the outcome through this signal — ShotHistoryExporter has been
    // connected to it for a long time — but nothing acted on the FAILURE case
    // (the exporter returns early on it), so a write to a shot that does not
    // exist logged one warning and the user's answer was gone.
    //
    // m_pendingMetadataWrites keeps this to writes this class initiated: every
    // other subsystem's writes arrive on the same signal. It is a REFCOUNT, not
    // a set. One reply can drive two writes for the same shot id (a rating and
    // bean metadata, see AIConversation), and with a set the first outcome
    // consumed the only entry, so the second — possibly the failing one — was
    // discarded as "not ours" by the very filter added to catch it.
    if (m_shotHistory) {
        connect(m_shotHistory, &ShotHistoryStorage::shotMetadataUpdated, this,
                [this](qint64 shotId, bool success) {
            const auto it = m_pendingMetadataWrites.find(shotId);
            if (it == m_pendingMetadataWrites.end()) return;  // not ours
            if (--it.value() <= 0)
                m_pendingMetadataWrites.erase(it);
            if (success) return;
            // Do NOT assert a cause here. `success == false` also arrives from a
            // not-ready database, from no valid fields to update, and from a
            // prepare or exec failure — only one of those is "no such shot".
            // Field logs are read and acted on by users' own AI assistants, so a
            // confident wrong diagnosis costs more than a neutral one.
            qWarning() << "AIManager: metadata write to shot" << shotId
                       << "did not land, so what the user told the advisor was not saved."
                       << "Leading candidate is a conversation turn still naming a shot id"
                       << "from a database this device no longer has; a database that was"
                       << "not ready, or an SQL failure, produce the same result.";
            emit shotMetadataCaptureFailed(shotId);
        });
    }
}


// File-scope helper: render one `recentAdvice` entry (see
// DialingBlocks::buildRecentAdviceBlock) as a markdown block. Every other
// section of the in-app historicalContext is hand-rendered prose, not a
// JSON blob, so this keeps the format consistent — the underlying data is
// identical to what the MCP `recentAdvice` JSON array carries for the same
// inputs (turnsAgo/recommendation/structuredNext/userResponse).

void AIManager::requestRecentShotContext(const QVariant& shotData, int excludeShotId)
{
    const ShotProjection shot = coerceShot(shotData);
    if (!m_shotHistory || (shot.beanBrand.isEmpty() && shot.profileName.isEmpty())) {
        emit recentShotContextReady(QString());
        return;
    }

    const QString dbPath = m_shotHistory->databasePath();
    QPointer<AIManager> self(this);
    ++m_contextSerial;
    int serial = m_contextSerial;

    // NOTE: QPointer is NOT thread-safe — it tracks QObject destruction via the main
    // event loop. The background thread captures `self` by value but MUST NOT dereference
    // it. All dereferences occur inside the QueuedConnection callback, which runs on the
    // main thread where QPointer's tracking is valid.
    QThread* thread = QThread::create([self, dbPath, shot, excludeShotId, serial]() {
        DialingBlocks::AdvisorContextBlocks blocks;
        withTempDb(dbPath, "ai_advisor_ctx", [&](QSqlDatabase& db) {
            // The shot the advice is about, read whole — the same record
            // ai_advisor_invoke resolves, so both surfaces feed the one
            // assembler identical input.
            const ShotRecord record =
                ShotHistoryStorage::loadShotRecordStatic(db, excludeShotId);
            const ShotProjection ctxShot = ShotHistoryStorage::convertShotRecord(record);

            // Loaded here rather than inside the assembler: see
            // buildAdvisorContextBlocks on why it is a parameter.
            QList<AIConversation::HistoricalAssistantTurn> turns;
            if (!ctxShot.profileKbId.isEmpty()) {
                turns = AIConversation::loadRecentAssistantTurnsForKey(
                    AIManager::conversationKey(ctxShot), DialingBlocks::kRecentAdviceTurns);
            }
            blocks = DialingBlocks::buildAdvisorContextBlocks(
                db, ctxShot, excludeShotId, turns);
        });

        QMetaObject::invokeMethod(qApp, [self, serial, excludeShotId, blocks]() {
            if (!self) return;
            self->emitRecentShotContext(blocks, excludeShotId, serial);
        }, Qt::QueuedConnection);
    });

    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void AIManager::emitRecentShotContext(const DialingBlocks::AdvisorContextBlocks& blocks,
                                      qint64 contextShotId,
                                      int serial)
{
    if (serial != m_contextSerial) {
        // Stale request superseded by a newer one — emit empty so QML clears
        // contextLoading, and leave the cache alone: the newer request owns it.
        emit recentShotContextReady(QString());
        return;
    }

    m_contextBlocks = blocks;
    m_contextBlocksShotId = contextShotId;

    // The signal carries the blocks so the web surface and the tests can see what
    // resolved. The value QML sends is assembled by buildConversationUserPrompt at
    // send time, from the cache above — the two must not be assembled twice.
    QJsonObject ctx;
    if (!blocks.dialInSessions.isEmpty()) ctx["dialInSessions"] = blocks.dialInSessions;
    if (!blocks.bestRecentShot.isEmpty()) ctx["bestRecentShot"] = blocks.bestRecentShot;
    if (!blocks.grinderContext.isEmpty()) ctx["grinderContext"] = blocks.grinderContext;
    if (!blocks.grinderCalibration.isEmpty()) ctx["grinderCalibration"] = blocks.grinderCalibration;
    if (!blocks.recentAdvice.isEmpty()) ctx["recentAdvice"] = blocks.recentAdvice;

    emit recentShotContextReady(
        ctx.isEmpty() ? QString()
                      : QString::fromUtf8(QJsonDocument(ctx).toJson(QJsonDocument::Compact)));
}

QString AIManager::buildConversationUserPrompt(const QVariant& shotData,
                                               const QString& question,
                                               const QString& shotLabel)
{
    const ShotProjection shot = coerceShot(shotData);
    if (!shot.isValid()) return question;

    // The shot's own payload, from the same builder ai_advisor_invoke uses.
    ShotSummary summary = m_summarizer->summarizeFromHistory(shot);
    QJsonParseError err{};
    const QJsonDocument doc =
        QJsonDocument::fromJson(m_summarizer->buildUserPrompt(summary).toUtf8(), &err);
    QJsonObject payload = (err.error == QJsonParseError::NoError && doc.isObject())
                              ? doc.object()
                              : QJsonObject();

    // Context blocks, only when they were resolved for THIS shot. A mismatch means
    // the user opened another shot while the request was in flight; sending the
    // other shot's history would be worse than sending none.
    if (m_contextBlocksShotId == shot.id) {
        enrichUserPromptObject(payload, shot, m_contextBlocks);
    } else if (shot.isValid()) {
        // sawPrediction does not come from the DB pass, so it is available either way.
        const QJsonObject sawPrediction =
            DialingBlocks::buildSawPredictionBlock(m_settings, m_profileManager, shot);
        if (!sawPrediction.isEmpty()) payload["sawPrediction"] = sawPrediction;
    }

    if (!shotLabel.isEmpty()) payload["shotLabel"] = shotLabel;

    // What moved since the previous shot in this conversation. A field, not a
    // banner prepended to the text — the banner is what made the payload a
    // prose/JSON sandwich that then had to be taken apart again to be read.
    if (m_conversation) {
        const QString soFar =
            QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact));
        const QJsonObject changes = m_conversation->changesFromPreviousShot(shotLabel, soFar);
        if (!changes.isEmpty()) payload["changesFromPreviousShotInConversation"] = changes;
    }

    if (!question.isEmpty()) payload["question"] = question;

    return QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact));
}

void AIManager::testConnection()
{
    AIProvider* provider = currentProvider();
    if (!provider) {
        m_lastTestResult = tr_("ai.error.noProviderSelected", "No AI provider selected");
        m_lastTestSuccess = false;
        emit testResultChanged();
        return;
    }

    provider->testConnection();
}

void AIManager::analyze(const QString& systemPrompt, const QString& userPrompt)
{
    if (m_analyzing) {
        m_lastError = tr_("ai.error.analysisInProgress", "Analysis already in progress");
        emit errorOccurred(m_lastError);
        return;
    }

    AIProvider* provider = currentProvider();
    if (!provider) {
        m_lastError = tr_("ai.error.noProviderConfigured", "No AI provider configured");
        emit errorOccurred(m_lastError);
        return;
    }

    if (!isConfigured()) {
        m_lastError = tr_("ai.error.providerNotConfigured", "AI provider not configured");
        emit errorOccurred(m_lastError);
        return;
    }

    m_analyzing = true;
    m_isConversationRequest = false;
    m_isBagExtractionRequest = false;
    emit analyzingChanged();

    // Store for logging
    m_lastSystemPrompt = systemPrompt;
    m_lastUserPrompt = userPrompt;

    logPrompt(selectedProvider(), systemPrompt, userPrompt);
    provider->analyze(systemPrompt, userPrompt);
}

void AIManager::extractCoffeeBagDetails(const QString& requestToken, const QString& pageText,
                                        const QString& kind)
{
    if (m_analyzing) {
        emit bagDetailsExtractionFailed(requestToken, QStringLiteral("busy"));
        return;
    }
    AIProvider* provider = currentProvider();
    if (!provider || !isConfigured()) {
        emit bagDetailsExtractionFailed(requestToken, QStringLiteral("notConfigured"));
        return;
    }

    // Extraction contract mirrors Visualizer's "Get info": page text in, a
    // flat JSON object of only-what-the-page-states out. Keys = the blob
    // vocabulary so the caller can merge without remapping.
    static const QString kCoffeePrompt = QStringLiteral(
        "You extract coffee bag details from the plain text of a roaster's product page. "
        "Reply with ONLY a JSON object - no markdown, no commentary. Use exactly these keys, "
        "omitting any the page does not clearly state: origin (country), region, farm, "
        "producer (person or company that grew it), variety, elevation (display string, e.g. "
        "\"1900-2100 m\"), process (e.g. \"Washed\", \"Natural\"), harvest (e.g. \"Late 2025\"), "
        "roastLevel (one of: Light, Medium-Light, Medium, Medium-Dark, Dark - map the page's "
        "wording), tastingNotes (comma-separated flavor descriptors from the page). "
        "Never guess or infer a value the text does not state. For blends without a stated "
        "origin, leave origin out and describe the blend in variety if stated.");
    // Tea vocabulary (add-recipe-wizard-tea): descriptive keys renamed for
    // tea (garden/cultivar/flush) plus STRUCTURED brewing numbers the recipe
    // wizard seeds from — brewTempC must be Celsius (the model converts °F
    // and boiling-water wordings), leafGramsPer100Ml must be normalized from
    // per-cup dosing (1 cup = 237 ml unless the page defines one).
    static const QString kTeaPrompt = QStringLiteral(
        "You extract loose-leaf tea details from the plain text of a tea vendor's product page. "
        "Reply with ONLY a JSON object - no markdown, no commentary. Use exactly these keys, "
        "omitting any the page does not clearly state: teaType (one of: black, green, oolong, "
        "white, herbal, pu-erh - map the page's wording; a tisane or infusion is herbal), "
        "origin (country), region, garden (the estate or garden name), cultivar, flush (the "
        "harvest or flush, e.g. \"First flush 2026\", \"Spring 2026\"), tastingNotes "
        "(comma-separated flavor descriptors from the page), brewTempC (NUMBER, Celsius - "
        "convert Fahrenheit, e.g. 212 -> 100; \"boiling\" or \"freshly-boiled\" -> 100), "
        "leafGramsPer100Ml (NUMBER - normalize the stated leaf dose to grams per 100 ml of "
        "water; treat one cup as 237 ml unless the page defines a cup), steepTime (display "
        "string, e.g. \"3-5 minutes\"). "
        "Never guess or infer a value the text does not state - in particular, never invent "
        "brewing numbers the page does not give.");

    const QString& systemPrompt =
        (kind == QLatin1String("tea")) ? kTeaPrompt : kCoffeePrompt;

    m_analyzing = true;
    m_isConversationRequest = false;
    m_isBagExtractionRequest = true;
    m_bagExtractionToken = requestToken;
    emit analyzingChanged();

    m_lastSystemPrompt = systemPrompt;
    m_lastUserPrompt = QStringLiteral("[Bag page text from %1, %2 chars]")
                           .arg(requestToken).arg(pageText.size());
    logPrompt(selectedProvider(), systemPrompt, m_lastUserPrompt);
    provider->analyze(systemPrompt, pageText);
}

bool AIManager::supportsUrlExtraction() const
{
    AIProvider* provider = const_cast<AIManager*>(this)->currentProvider();
    return provider && provider->supportsUrlAnalysis();
}

void AIManager::extractCoffeeBagDetailsFromUrl(const QString& requestToken, const QString& url,
                                               const QString& kind)
{
    // Stage-2 extraction (add-recipe-wizard-tea): the local page fetch got
    // nothing (JS-rendered shop), so the PROVIDER fetches the URL itself via
    // its server-side web-fetch tool. Same JSON contract as stage 1 plus one
    // extra key: imageUrl (the main product photo) — SPA pages have no
    // og:image for the normal photo pipeline to find.
    if (m_analyzing) {
        emit bagDetailsExtractionFailed(requestToken, QStringLiteral("busy"));
        return;
    }
    AIProvider* provider = currentProvider();
    if (!provider || !isConfigured()) {
        emit bagDetailsExtractionFailed(requestToken, QStringLiteral("notConfigured"));
        return;
    }
    if (!provider->supportsUrlAnalysis()) {
        emit bagDetailsExtractionFailed(requestToken, QStringLiteral("urlFetchUnsupported"));
        return;
    }

    // Re-run extractCoffeeBagDetails' prompt selection with the stage-2
    // addendum. The prompts are function-local statics there; keep this
    // addendum in sync with the keys documented on parseBagExtraction.
    const QString base = (kind == QLatin1String("tea"))
        ? QStringLiteral(
            "You extract loose-leaf tea details from a tea vendor's product page. "
            "Reply with ONLY a JSON object - no markdown, no commentary. Use exactly these keys, "
            "omitting any the page does not clearly state: teaType (one of: black, green, oolong, "
            "white, herbal, pu-erh - map the page's wording; a tisane or infusion is herbal), "
            "origin (country), region, garden (the estate or garden name), cultivar, flush (the "
            "harvest or flush, e.g. \"First flush 2026\", \"Spring 2026\"), tastingNotes "
            "(comma-separated flavor descriptors from the page), brewTempC (NUMBER, Celsius - "
            "convert Fahrenheit, e.g. 212 -> 100; \"boiling\" or \"freshly-boiled\" -> 100), "
            "leafGramsPer100Ml (NUMBER - normalize the stated leaf dose to grams per 100 ml of "
            "water; treat one cup as 237 ml unless the page defines a cup), steepTime (display "
            "string, e.g. \"3-5 minutes\"). "
            "Never guess or infer a value the text does not state - in particular, never invent "
            "brewing numbers the page does not give.")
        : QStringLiteral(
            "You extract coffee bag details from a roaster's product page. "
            "Reply with ONLY a JSON object - no markdown, no commentary. Use exactly these keys, "
            "omitting any the page does not clearly state: origin (country), region, farm, "
            "producer (person or company that grew it), variety, elevation (display string, e.g. "
            "\"1900-2100 m\"), process (e.g. \"Washed\", \"Natural\"), harvest (e.g. \"Late 2025\"), "
            "roastLevel (one of: Light, Medium-Light, Medium, Medium-Dark, Dark - map the page's "
            "wording), tastingNotes (comma-separated flavor descriptors from the page). "
            "Never guess or infer a value the text does not state. For blends without a stated "
            "origin, leave origin out and describe the blend in variety if stated.");
    const QString systemPrompt = base + QStringLiteral(
        " Retrieve the product page URL in the user message yourself with your web tool first. "
        "Additionally include the key imageUrl (the MAIN product photo's absolute URL from the "
        "fetched page, when one is shown - an image file URL, never the page URL itself, and "
        "never a logo or banner).");
    const QString userPrompt = QStringLiteral(
        "Fetch this product page and extract the details: %1").arg(url);

    m_analyzing = true;
    m_isConversationRequest = false;
    m_isBagExtractionRequest = true;
    m_bagExtractionToken = requestToken;
    emit analyzingChanged();

    m_lastSystemPrompt = systemPrompt;
    m_lastUserPrompt = userPrompt;
    logPrompt(selectedProvider(), systemPrompt, userPrompt);
    provider->analyzeUrl(systemPrompt, userPrompt);
}

// static
QVariantMap AIManager::parseBagExtraction(const QString& response, bool* ok)
{
    if (ok)
        *ok = false;
    // Tolerate markdown fences / prose around the object: parse the first
    // '{' .. last '}' span.
    const qsizetype start = response.indexOf(QLatin1Char('{'));
    const qsizetype end = response.lastIndexOf(QLatin1Char('}'));
    if (start < 0 || end <= start)
        return {};
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(
        response.mid(start, end - start + 1).toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return {};

    static const QStringList kKeys{
        QStringLiteral("origin"), QStringLiteral("region"), QStringLiteral("farm"),
        QStringLiteral("producer"), QStringLiteral("variety"), QStringLiteral("elevation"),
        QStringLiteral("process"), QStringLiteral("harvest"), QStringLiteral("roastLevel"),
        QStringLiteral("tastingNotes"),
        // Tea vocabulary (add-recipe-wizard-tea). Union whitelist: the prompt
        // selected by bag kind controls which keys come back; this filter just
        // has to let both vocabularies through.
        QStringLiteral("teaType"), QStringLiteral("garden"), QStringLiteral("cultivar"),
        QStringLiteral("flush"), QStringLiteral("brewTempC"),
        QStringLiteral("leafGramsPer100Ml"), QStringLiteral("steepTime"),
        // Stage-2 only (extractCoffeeBagDetailsFromUrl): the product photo's
        // URL, consumed by the image cache — never a form field.
        QStringLiteral("imageUrl")};
    const QJsonObject obj = doc.object();
    QVariantMap fields;
    for (const QString& key : kKeys) {
        const QJsonValue raw = obj.value(key);
        QString value;
        if (raw.isArray()) {
            // Models frequently return tasting notes as an array despite the
            // prompt — join the scalar elements rather than dropping them.
            QStringList parts;
            const QJsonArray arr = raw.toArray();
            for (const QJsonValue& v : arr) {
                const QString part = v.toVariant().toString().trimmed();
                if (!part.isEmpty())
                    parts << part;
            }
            value = parts.join(QStringLiteral(", "));
        } else if (raw.isObject()) {
            qWarning() << "AIManager: bag extraction returned an object for" << key << "- skipped";
        } else {
            value = raw.toVariant().toString().trimmed();
        }
        // Cap per value: a prompt-injected page must not push multi-KB text
        // through a form field into the DB blob.
        if (!value.isEmpty())
            fields.insert(key, value.left(500));
    }
    // A non-empty object that yielded nothing usable is a response we could
    // not read, NOT an honest "the page states nothing" ({} stays a success).
    if (fields.isEmpty() && !obj.isEmpty())
        return {};
    if (ok)
        *ok = true;
    return fields;
}

void AIManager::analyzeConversation(const QString& systemPrompt, const QJsonArray& messages)
{
    if (m_analyzing) {
        emit conversationErrorOccurred(tr_("ai.error.analysisInProgress", "Analysis already in progress"));
        return;
    }

    AIProvider* provider = currentProvider();
    if (!provider) {
        m_lastError = tr_("ai.error.noProviderConfigured", "No AI provider configured");
        emit conversationErrorOccurred(m_lastError);
        return;
    }

    if (!isConfigured()) {
        m_lastError = tr_("ai.error.providerNotConfigured", "AI provider not configured");
        emit conversationErrorOccurred(m_lastError);
        return;
    }

    m_analyzing = true;
    m_isConversationRequest = true;
    m_isBagExtractionRequest = false;
    emit analyzingChanged();

    // Store for logging — flatten for the log file
    m_lastSystemPrompt = systemPrompt;
    m_lastUserPrompt = QString("[Conversation with %1 messages]").arg(messages.size());

    logPrompt(selectedProvider(), systemPrompt, m_lastUserPrompt);
    // Strip internal-only per-turn keys before the payload leaves the app.
    provider->analyzeConversation(systemPrompt, sanitizeApiMessages(messages));
}

void AIManager::refreshOllamaModels()
{
    auto* ollama = dynamic_cast<OllamaProvider*>(m_ollamaProvider.get());
    if (ollama) {
        ollama->refreshModels();
    }
}

void AIManager::onAnalysisComplete(const QString& response)
{
    m_analyzing = false;
    m_lastRecommendation = response;
    m_lastError.clear();

    // Log the successful response
    logResponse(selectedProvider(), response, true);

    emit analyzingChanged();

    // Emit to the appropriate listener based on request type
    if (m_isBagExtractionRequest) {
        m_isBagExtractionRequest = false;
        const QString token = m_bagExtractionToken;
        m_bagExtractionToken.clear();
        bool parsed = false;
        const QVariantMap fields = parseBagExtraction(response, &parsed);
        if (parsed)
            emit bagDetailsExtracted(token, fields);
        else
            emit bagDetailsExtractionFailed(token, QStringLiteral("unreadable"));
    } else if (m_isConversationRequest) {
        emit conversationResponseReceived(response);
    } else {
        emit recommendationReceived(response);
    }
}

void AIManager::onAnalysisFailed(const QString& error)
{
    m_analyzing = false;
    m_lastError = error;

    // Log the failed response
    logResponse(selectedProvider(), error, false);

    emit analyzingChanged();

    // Emit to the appropriate listener based on request type
    if (m_isBagExtractionRequest) {
        m_isBagExtractionRequest = false;
        const QString token = m_bagExtractionToken;
        m_bagExtractionToken.clear();
        emit bagDetailsExtractionFailed(token, error);
    } else if (m_isConversationRequest) {
        emit conversationErrorOccurred(error);
    } else {
        emit errorOccurred(error);
    }
}

void AIManager::onTestResult(bool success, const QString& message)
{
    m_lastTestSuccess = success;
    m_lastTestResult = message;
    emit testResultChanged();
}

void AIManager::onOllamaModelsRefreshed(const QStringList& models)
{
    m_ollamaModels = models;
    emit ollamaModelsChanged();
}

void AIManager::onSettingsChanged()
{
    // Update providers with new settings
    auto* openai = dynamic_cast<OpenAIProvider*>(m_openaiProvider.get());
    if (openai) {
        openai->setApiKey(m_settings->ai()->openaiApiKey());
        openai->setModel(m_settings->ai()->providerModel("openai"));  // empty → keeps default
        openai->setBaseUrl(m_settings->ai()->openaiEndpoint());
    }

    auto* anthropic = dynamic_cast<AnthropicProvider*>(m_anthropicProvider.get());
    if (anthropic) {
        anthropic->setApiKey(m_settings->ai()->anthropicApiKey());
        anthropic->setModel(m_settings->ai()->providerModel("anthropic"));  // empty → keeps default
        anthropic->setBaseUrl(m_settings->ai()->anthropicEndpoint());
    }

    auto* gemini = dynamic_cast<GeminiProvider*>(m_geminiProvider.get());
    if (gemini) {
        gemini->setApiKey(m_settings->ai()->geminiApiKey());
        gemini->setModel(m_settings->ai()->providerModel("gemini"));  // empty → keeps default
    }

    auto* openrouter = dynamic_cast<OpenRouterProvider*>(m_openrouterProvider.get());
    if (openrouter) {
        openrouter->setApiKey(m_settings->ai()->openrouterApiKey());
        openrouter->setModel(m_settings->ai()->openrouterModel());
    }

    auto* ollama = dynamic_cast<OllamaProvider*>(m_ollamaProvider.get());
    if (ollama) {
        ollama->setEndpoint(m_settings->ai()->ollamaEndpoint());
        ollama->setModel(m_settings->ai()->ollamaModel());
    }

    emit configurationChanged();
}

// ============================================================================
// Conversation Routing
// ============================================================================

QString AIManager::ConversationEntry::label() const
{
    QStringList bean;
    if (!beanBrand.isEmpty()) bean << beanBrand;
    if (!beanType.isEmpty()) bean << beanType;

    QStringList parts;
    if (!bean.isEmpty()) parts << bean.join(QStringLiteral(" "));
    if (!profileName.isEmpty()) parts << profileName;
    if (!equipmentLabel.isEmpty()) parts << equipmentLabel;
    return parts.join(QStringLiteral(" / "));
}

QJsonObject AIManager::ConversationEntry::toJson() const
{
    QJsonObject obj;
    obj["key"] = key;
    obj["beanBrand"] = beanBrand;
    obj["beanType"] = beanType;
    obj["profileName"] = profileName;
    obj["equipmentLabel"] = equipmentLabel;
    obj["equipmentId"] = equipmentId;
    obj["timestamp"] = timestamp;
    return obj;
}

AIManager::ConversationEntry AIManager::ConversationEntry::fromJson(const QJsonObject& obj)
{
    ConversationEntry entry;
    entry.key = obj["key"].toString();
    entry.beanBrand = obj["beanBrand"].toString();
    entry.beanType = obj["beanType"].toString();
    entry.profileName = obj["profileName"].toString();
    entry.equipmentLabel = obj["equipmentLabel"].toString();
    entry.equipmentId = obj["equipmentId"].toVariant().toLongLong();
    entry.timestamp = obj["timestamp"].toVariant().toLongLong();
    return entry;
}

AIManager::ConversationEntry AIManager::conversationEntry(const QString& key) const
{
    for (const auto& entry : m_conversationIndex) {
        if (entry.key == key)
            return entry;
    }
    return {};
}

QString AIManager::conversationKey(const ShotProjection& shot)
{
    // The package is part of the identity because a saved thread REPLAYS its
    // stored turns on every request. Scoping the payload alone leaves older turns
    // describing another basket inside the same transcript, still informing every
    // answer. equipmentId 0 is the unpackaged pool, matching AdviceScope.
    QString normalized = shot.beanBrand.toLower().trimmed() + "|" +
                         shot.beanType.toLower().trimmed() + "|" +
                         shot.profileName.toLower().trimmed() + "|" +
                         QString::number(shot.equipmentId);
    QByteArray hash = QCryptographicHash::hash(normalized.toUtf8(), QCryptographicHash::Sha1);
    return hash.toHex().left(16);
}

void AIManager::loadConversationIndex()
{
    AppSettings settings;
    QByteArray indexJson = settings.value("ai/conversations/index").toByteArray();
    m_conversationIndex.clear();

    if (!indexJson.isEmpty()) {
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(indexJson, &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            qWarning() << "AIManager::loadConversationIndex: JSON parse error:" << parseError.errorString();
        } else if (doc.isArray()) {
            QJsonArray arr = doc.array();
            for (const QJsonValue& val : arr) {
                ConversationEntry entry = ConversationEntry::fromJson(val.toObject());
                if (entry.key.isEmpty()) {
                    qWarning() << "AIManager::loadConversationIndex: Skipping entry with empty key";
                    continue;
                }
                m_conversationIndex.append(entry);
            }
        }
    }
    qDebug() << "AIManager: Loaded conversation index with" << m_conversationIndex.size() << "entries";
}

void AIManager::saveConversationIndex()
{
    QJsonArray arr;
    for (const auto& entry : m_conversationIndex) {
        arr.append(entry.toJson());
    }
    AppSettings settings;
    settings.setValue("ai/conversations/index", QJsonDocument(arr).toJson(QJsonDocument::Compact));
    emit conversationIndexChanged();
}

void AIManager::touchConversationEntry(const QString& key)
{
    qint64 now = QDateTime::currentSecsSinceEpoch();
    for (int i = 0; i < m_conversationIndex.size(); i++) {
        if (m_conversationIndex[i].key == key) {
            m_conversationIndex[i].timestamp = now;
            // Move to front (most recent)
            if (i > 0) {
                auto entry = m_conversationIndex.takeAt(i);
                m_conversationIndex.prepend(entry);
            }
            saveConversationIndex();
            return;
        }
    }
}

void AIManager::evictOldestConversation()
{
    if (m_conversationIndex.size() < MAX_CONVERSATIONS) return;

    // Remove the last (oldest) entry
    ConversationEntry oldest = m_conversationIndex.takeLast();

    // Remove its QSettings data
    AppSettings settings;
    QString prefix = "ai/conversations/" + oldest.key + "/";
    settings.remove(prefix + "systemPrompt");
    settings.remove(prefix + "messages");
    settings.remove(prefix + "timestamp");

    qDebug() << "AIManager: Evicted oldest conversation:" << oldest.beanBrand << oldest.beanType << oldest.profileName;
    saveConversationIndex();
}

void AIManager::clearAllConversationsOnce(const QString& migrationId)
{
    AppSettings settings;
    const QString markerKey = QStringLiteral("ai/migrations/") + migrationId;
    if (settings.value(markerKey).toBool())
        return;

    settings.beginGroup(QStringLiteral("ai/conversations"));
    settings.remove(QString());  // removes all keys in this group
    settings.endGroup();

    settings.setValue(markerKey, true);
    qDebug() << "AIManager: cleared all conversations for migration" << migrationId;
}

void AIManager::migrateFromLegacyConversation()
{
    AppSettings settings;

    // Check if legacy data exists and new index doesn't
    QByteArray legacyMessages = settings.value("ai/conversation/messages").toByteArray();
    QByteArray existingIndex = settings.value("ai/conversations/index").toByteArray();

    if (legacyMessages.isEmpty() || !existingIndex.isEmpty()) return;

    QJsonDocument doc = QJsonDocument::fromJson(legacyMessages);
    if (!doc.isArray() || doc.array().isEmpty()) return;

    qDebug() << "AIManager: Migrating legacy conversation to keyed storage";

    // Use a fixed key for the legacy conversation
    QString legacyKey = "_legacy";

    // Copy data to new keyed location
    QString prefix = "ai/conversations/" + legacyKey + "/";
    settings.setValue(prefix + "systemPrompt", settings.value("ai/conversation/systemPrompt"));
    settings.setValue(prefix + "messages", legacyMessages);
    settings.setValue(prefix + "timestamp", settings.value("ai/conversation/timestamp"));

    // Create index entry
    ConversationEntry entry;
    entry.key = legacyKey;
    entry.beanBrand = "";
    entry.beanType = "";
    entry.profileName = "(Previous conversation)";
    entry.timestamp = QDateTime::currentSecsSinceEpoch();

    QJsonArray indexArr;
    indexArr.append(entry.toJson());
    settings.setValue("ai/conversations/index", QJsonDocument(indexArr).toJson(QJsonDocument::Compact));

    // Keep legacy keys as recovery fallback — they'll be harmless if left in place
    // settings.remove("ai/conversation/systemPrompt");
    // settings.remove("ai/conversation/messages");
    // settings.remove("ai/conversation/timestamp");

    qDebug() << "AIManager: Legacy conversation migrated to key:" << legacyKey;
}

QString AIManager::switchConversation(const QVariant& shotData)
{
    const ShotProjection shot = coerceShot(shotData);
    const QString key = conversationKey(shot);

    // Already on this key — just touch LRU
    if (m_conversation->storageKey() == key) {
        touchConversationEntry(key);
        return key;
    }

    // Refuse if busy
    if (m_conversation->isBusy()) {
        qWarning() << "AIManager: Cannot switch conversation while busy";
        return m_conversation->storageKey();
    }

    // Save current conversation if it has history
    if (m_conversation->hasHistory()) {
        m_conversation->saveToStorage();
    }

    // Clear in-memory state without touching QSettings (clearHistory() would delete stored data)
    m_conversation->resetInMemory();

    // Check if key exists in index
    bool exists = false;
    for (const auto& entry : m_conversationIndex) {
        if (entry.key == key) {
            exists = true;
            break;
        }
    }

    // Set new storage key and load whatever is actually on disk for it —
    // regardless of `exists` (m_conversationIndex only tracks conversations
    // the IN-APP flow has touched before; it's never updated by the MCP
    // ai_advisor_invoke path's AIConversation::appendAssistantTurnForKey,
    // which writes turns directly to QSettings). Without this, a key with
    // real MCP-written turns but no in-app history looks empty here,
    // hasHistory() reads false, QML calls ask() instead of followUp(), and
    // the next saveToStorage() silently destroys those turns — the root
    // cause of the persistence gap found in manual verification of
    // fix-multishot-advice-tracking (loadFromStorage is a safe no-op when
    // the key genuinely has nothing on disk).
    ConversationEntry entry;
    entry.key = key;
    entry.beanBrand = shot.beanBrand;
    entry.beanType = shot.beanType;
    entry.profileName = shot.profileName;
    entry.equipmentLabel = shot.equipmentLabel();
    entry.equipmentId = shot.equipmentId;
    entry.timestamp = QDateTime::currentSecsSinceEpoch();

    m_conversation->setStorageKey(key);
    m_conversation->setContextLabel(entry.label());
    m_conversation->loadFromStorage();

    if (exists) {
        touchConversationEntry(key);
    } else {
        evictOldestConversation();
        m_conversationIndex.prepend(entry);
        saveConversationIndex();
    }

    emit m_conversation->savedConversationChanged();
    qDebug() << "AIManager: Switched to conversation key:" << key << "(" << entry.label() << ")";
    return key;
}

void AIManager::loadMostRecentConversation()
{
    if (m_conversationIndex.isEmpty()) {
        m_conversation->setStorageKey(QString());
        m_conversation->setContextLabel(QString());
        return;
    }

    const auto& entry = m_conversationIndex.first();
    m_conversation->setStorageKey(entry.key);
    m_conversation->setContextLabel(entry.label());
    m_conversation->loadFromStorage();
    qDebug() << "AIManager: Loaded most recent conversation:" << entry.key
             << "(" << entry.label() << ")";
}

void AIManager::clearCurrentConversation()
{
    QString key = m_conversation->storageKey();
    m_conversation->clearHistory();

    // Remove the entry from the conversation index
    if (!key.isEmpty()) {
        for (int i = 0; i < m_conversationIndex.size(); i++) {
            if (m_conversationIndex[i].key == key) {
                m_conversationIndex.removeAt(i);
                saveConversationIndex();
                break;
            }
        }
    }
}

bool AIManager::isSupportedBeverageType(const QString& beverageType) const
{
    QString bev = beverageType.toLower().trimmed();
    return bev.isEmpty() || bev == "espresso" || bev == "filter" || bev == "pourover";
}

bool AIManager::isMistakeShot(const QVariant& shotVariant) const
{
    const ShotProjection shotData = coerceShot(shotVariant);
    if (shotData.durationSec < 10.0) return true;
    if (shotData.finalWeightG < 5.0) return true;
    if (shotData.targetWeightG > 0.0 && shotData.finalWeightG < shotData.targetWeightG / 3.0) return true;
    return false;
}

// ============================================================================
// Logging
// ============================================================================

QString AIManager::logPath() const
{
    QString basePath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (basePath.isEmpty()) {
        basePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    }
    QString aiLogPath = basePath + "/ai_logs";
    QDir().mkpath(aiLogPath);
    return aiLogPath;
}

void AIManager::logPrompt(const QString& provider, const QString& systemPrompt, const QString& userPrompt)
{
    // Store for pairing with response
    m_lastSystemPrompt = systemPrompt;
    m_lastUserPrompt = userPrompt;

    QString path = logPath();
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss");

    // Write individual prompt file
    QString promptFile = path + "/prompt_" + timestamp + ".txt";
    QFile file(promptFile);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out << "=== AI PROMPT LOG ===\n";
        out << "Timestamp: " << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";
        out << "Provider: " << provider << "\n";
        out << "\n=== SYSTEM PROMPT ===\n\n";
        out << systemPrompt << "\n";
        out << "\n=== USER PROMPT ===\n\n";
        out << userPrompt << "\n";
        file.close();
        qDebug() << "AI: Logged prompt to" << promptFile;
    } else {
        qWarning() << "AI: Failed to write prompt log:" << file.errorString();
    }

    // Also append to conversation history
    QString historyFile = path + "/conversation_history.txt";
    QFile history(historyFile);
    if (history.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&history);
        out << "\n" << QString("=").repeated(80) << "\n";
        out << "PROMPT - " << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";
        out << "Provider: " << provider << "\n";
        out << QString("-").repeated(40) << "\n";
        out << userPrompt << "\n";
        history.close();
    } else {
        qWarning() << "AI: Failed to append to conversation history:" << history.errorString();
    }
}

void AIManager::logResponse(const QString& provider, const QString& response, bool success)
{
    QString path = logPath();
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss");

    // Write individual response file
    QString responseFile = path + "/response_" + timestamp + ".txt";
    QFile file(responseFile);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out << "=== AI RESPONSE LOG ===\n";
        out << "Timestamp: " << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";
        out << "Provider: " << provider << "\n";
        out << "Success: " << (success ? "Yes" : "No") << "\n";
        out << "\n=== RESPONSE ===\n\n";
        out << response << "\n";
        file.close();
        qDebug() << "AI: Logged response to" << responseFile;
    } else {
        qWarning() << "AI: Failed to write response log:" << file.errorString();
    }

    // Write complete Q&A file (prompt + response together)
    QString qaFile = path + "/qa_" + timestamp + ".txt";
    QFile qa(qaFile);
    if (qa.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&qa);
        out << "=== AI Q&A LOG ===\n";
        out << "Timestamp: " << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";
        out << "Provider: " << provider << "\n";
        out << "Success: " << (success ? "Yes" : "No") << "\n";
        out << "\n" << QString("=").repeated(60) << "\n";
        out << "SYSTEM PROMPT\n";
        out << QString("=").repeated(60) << "\n\n";
        out << m_lastSystemPrompt << "\n";
        out << "\n" << QString("=").repeated(60) << "\n";
        out << "USER PROMPT\n";
        out << QString("=").repeated(60) << "\n\n";
        out << m_lastUserPrompt << "\n";
        out << "\n" << QString("=").repeated(60) << "\n";
        out << "AI RESPONSE\n";
        out << QString("=").repeated(60) << "\n\n";
        out << response << "\n";
        qa.close();
        qDebug() << "AI: Logged Q&A to" << qaFile;
    } else {
        qWarning() << "AI: Failed to write Q&A log:" << qa.errorString();
    }

    // Also append to conversation history
    QString historyFile = path + "/conversation_history.txt";
    QFile history(historyFile);
    if (history.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&history);
        out << QString("-").repeated(40) << "\n";
        out << "RESPONSE - " << (success ? "SUCCESS" : "FAILED") << "\n";
        out << QString("-").repeated(40) << "\n";
        out << response << "\n";
        history.close();
    } else {
        qWarning() << "AI: Failed to append to conversation history:" << history.errorString();
    }
}
