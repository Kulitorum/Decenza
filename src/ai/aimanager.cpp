#include "aimanager.h"
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

#include <QNetworkAccessManager>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QDateTime>
#include <QLocale>
#include <QDebug>
#include <QCryptographicHash>
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
    connect(openai, &AIProvider::analysisComplete, this, &AIManager::onAnalysisComplete);
    connect(openai, &AIProvider::analysisFailed, this, &AIManager::onAnalysisFailed);
    connect(openai, &AIProvider::testResult, this, &AIManager::onTestResult);
    m_openaiProvider.reset(openai);

    // Create Anthropic provider
    QString anthropicKey = m_settings->ai()->anthropicApiKey();
    auto* anthropic = new AnthropicProvider(m_networkManager, anthropicKey, this);
    connect(anthropic, &AIProvider::analysisComplete, this, &AIManager::onAnalysisComplete);
    connect(anthropic, &AIProvider::analysisFailed, this, &AIManager::onAnalysisFailed);
    connect(anthropic, &AIProvider::testResult, this, &AIManager::onTestResult);
    m_anthropicProvider.reset(anthropic);

    // Create Gemini provider
    QString geminiKey = m_settings->ai()->geminiApiKey();
    auto* gemini = new GeminiProvider(m_networkManager, geminiKey, this);
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

// Heuristic for "the prior assistant message asked the user about
// taste". The shot-analysis system prompt instructs the model to ask
// for a 1-100 score when tastingFeedback.hasEnjoymentScore is false;
// the prose typically carries phrases like "how did this taste",
// "score", "1-100", or asks for "tasting notes". Conservative: false
// negative is fine (we just don't auto-persist; user can rate via the
// editor or QuickRatingRow). False positive risk: a reply that happens
// to contain a number gets attached as a rating to the wrong shot —
// guarded by the shotId binding so the writeback hits the same shot
// the conversation is anchored to, not a random one.
static bool priorAssistantAskedAboutTaste(const QString& priorAssistantMessage)
{
    if (priorAssistantMessage.isEmpty()) return false;
    const QString lc = priorAssistantMessage.toLower();
    // Any one of these phrases is a strong signal the model is asking
    // the user for a numeric/text taste rating.
    static const QStringList markers{
        QStringLiteral("how did"),
        QStringLiteral("how does it taste"),
        QStringLiteral("tasting notes"),
        QStringLiteral("1-100"),
        QStringLiteral("1 to 100"),
        QStringLiteral("score"),
        QStringLiteral("rate this shot"),
        QStringLiteral("rate the shot"),
        QStringLiteral("how would you rate"),
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
    m_shotHistory->requestUpdateShotMetadata(shotId, metadata);
}

std::optional<AIManager::UserRatingReply> AIManager::parseUserRatingReply(const QString& reply)
{
    // Find the first numeric token in [1, 100]. The token may be a bare
    // integer ("82"), a decimal ("82.5" → rounds to 83), or have an
    // optional suffix `/100`, `out of 100`, `%` (suffix consumed when
    // present but not required). The remaining text — minus the token
    // and any consumed suffix — is trimmed and returned as notes.
    if (reply.trimmed().isEmpty()) return std::nullopt;

    static const QRegularExpression rx(QStringLiteral(
        "(\\d+(?:\\.\\d+)?)\\s*"
        "(/\\s*100|out\\s*of\\s*100|%)?"),
        QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator it = rx.globalMatch(reply);
    while (it.hasNext()) {
        QRegularExpressionMatch m = it.next();
        bool ok = false;
        const double raw = m.captured(1).toDouble(&ok);
        if (!ok) continue;
        const int rounded = static_cast<int>(std::round(raw));
        if (rounded < 1 || rounded > 100) continue;

        // Reject negative numbers: the regex captures the digits without
        // the leading minus, but if the character immediately before the
        // match is a minus sign the user clearly wrote a negative.
        const qsizetype matchStartCheck = m.capturedStart(1);
        if (matchStartCheck > 0) {
            const QChar prev = reply.at(matchStartCheck - 1);
            if (prev == QLatin1Char('-') || prev == QChar(0x2212)) continue;
        }

        UserRatingReply out;
        out.score = rounded;
        const qsizetype matchStart = m.capturedStart(0);
        const qsizetype matchEnd = m.capturedEnd(0);
        QString remaining = reply.left(matchStart) + reply.mid(matchEnd);
        // Strip punctuation/whitespace from the edges so "82, balanced
        // and sweet" → notes "balanced and sweet".
        static const QRegularExpression edgeTrim(QStringLiteral(
            "^[\\s,;:\\-—.!?]+|[\\s,;:\\-—.!?]+$"));
        remaining.replace(edgeTrim, QString());
        out.notes = remaining.trimmed();
        return out;
    }
    return std::nullopt;
}

ShotMetadata AIManager::buildMetadata(const QString& beanBrand,
                                       const QString& beanType,
                                       const QString& roastDate,
                                       const QString& roastLevel,
                                       const QString& grinderBrand,
                                       const QString& grinderModel,
                                       const QString& grinderBurrs,
                                       const QString& grinderSetting,
                                       int enjoymentScore,
                                       const QString& tastingNotes) const
{
    ShotMetadata metadata;
    metadata.beanBrand = beanBrand;
    metadata.beanType = beanType;
    metadata.roastDate = roastDate;
    metadata.roastLevel = roastLevel;
    metadata.grinderBrand = grinderBrand;
    metadata.grinderModel = grinderModel;
    metadata.grinderBurrs = grinderBurrs;
    metadata.grinderSetting = grinderSetting;
    metadata.espressoEnjoyment = enjoymentScore;
    metadata.espressoNotes = tastingNotes;
    return metadata;
}

void AIManager::analyzeShot(ShotDataModel* shotData,
                             Profile* profile,
                             double doseWeight,
                             double finalWeight,
                             const QVariantMap& metadata)
{
    // Extract metadata from QVariantMap (QML-friendly)
    analyzeShotWithMetadata(shotData, profile, doseWeight, finalWeight,
        metadata.value("beanBrand").toString(),
        metadata.value("beanType").toString(),
        metadata.value("roastDate").toString(),
        metadata.value("roastLevel").toString(),
        metadata.value("grinderBrand").toString(),
        metadata.value("grinderModel").toString(),
        metadata.value("grinderBurrs").toString(),
        metadata.value("grinderSetting").toString(),
        metadata.value("enjoymentScore").toInt(),
        metadata.value("tastingNotes").toString());
}

void AIManager::analyzeShotWithMetadata(ShotDataModel* shotData,
                             const Profile* profile,
                             double doseWeight,
                             double finalWeight,
                             const QString& beanBrand,
                             const QString& beanType,
                             const QString& roastDate,
                             const QString& roastLevel,
                             const QString& grinderBrand,
                             const QString& grinderModel,
                             const QString& grinderBurrs,
                             const QString& grinderSetting,
                             int enjoymentScore,
                             const QString& tastingNotes)
{
    if (!isConfigured()) {
        m_lastError = "AI provider not configured. Please add your API key in settings.";
        emit errorOccurred(m_lastError);
        return;
    }

    if (!shotData) {
        m_lastError = "No shot data available";
        emit errorOccurred(m_lastError);
        return;
    }

    // Check beverage type — only espresso, filter, and pourover are supported
    if (profile) {
        QString bevType = profile->beverageType().toLower();
        if (bevType != "espresso" && bevType != "filter" && bevType != "pourover" && !bevType.isEmpty()) {
            m_lastError = QString("AI analysis isn't available for %1 profiles yet — only espresso and filter are supported for now. Sorry about that!")
                .arg(profile->beverageType());
            emit errorOccurred(m_lastError);
            return;
        }
    }

    // Build metadata and summarize shot. Ensure dC/dt is available for
    // channeling analysis (idempotent; usually already run by the save path).
    shotData->computeConductanceDerivative();
    ShotMetadata metadata = buildMetadata(beanBrand, beanType, roastDate, roastLevel,
                                          grinderBrand, grinderModel, grinderBurrs,
                                          grinderSetting, enjoymentScore, tastingNotes);
    ShotSummary summary = m_summarizer->summarize(shotData, profile, metadata, doseWeight, finalWeight);

    // Build prompts (select system prompt based on beverage type + profile knowledge)
    // profileKbId is the direct knowledge base key; profileType is the fallback for custom titles
    QString systemPrompt = ShotSummarizer::shotAnalysisSystemPrompt(
        summary.beverageType, summary.profileTitle, summary.profileType, summary.profileKbId);

    // The user prompt is built as a JSON envelope. The four DB-scoped
    // blocks (dialInSessions / bestRecentShot / grinderContext /
    // sawPrediction) are merged in below before serialization, so the
    // shotAnalysis system prompt's references to those structured fields
    // land on real keys. See openspec add-dialing-blocks-to-advisor.
    QJsonObject userPromptObj = m_summarizer->buildUserPromptObject(summary);

    // Fetch recent shot history + four dialing blocks on a background
    // thread, then send to AI on callback. SAW prediction is built on
    // the main thread (it touches Settings::calibration() and
    // ProfileManager).
    if (m_shotHistory) {
        const QString dbPath = m_shotHistory->databasePath();
        const QString kbId = summary.profileKbId;
        const qint64 excludeId = m_shotHistory->lastSavedShotId();
        QPointer<AIManager> self(this);

        // Snapshot the current conversation's recent assistant turns on
        // the main thread BEFORE the background closure starts. Carrying
        // them into the bg thread is safe (they're plain-data structs);
        // touching m_conversation from the bg thread would be a race.
        // Issue #1053 — closed-loop recentAdvice block.
        QList<AIConversation::HistoricalAssistantTurn> recentTurns;
        if (m_conversation) {
            recentTurns = m_conversation->recentAssistantTurns(3);
        }

        // Latch the resolved shot id onto the conversation pair so
        // future turns can attribute this advisor turn to the shot
        // (#1053 setShotIdForCurrentTurn — applies to the latest user
        // message and stamps the next assistant message).
        if (m_conversation && excludeId > 0) {
            m_conversation->setShotIdForCurrentTurn(excludeId);
        }

        QThread* thread = QThread::create([self, dbPath, kbId, excludeId, systemPrompt, userPromptObj,
                                            recentTurns]() mutable {
            QVariantList recentShots;
            QJsonArray dialInSessions;
            QJsonObject bestRecentShot;
            QJsonObject grinderContext;
            QJsonArray recentAdvice;
            ShotProjection resolvedShot;

            withTempDb(dbPath, "ai_recent", [&](QSqlDatabase& db) {
                if (!kbId.isEmpty())
                    recentShots = ShotHistoryStorage::loadRecentShotsByKbIdStatic(db, kbId, 5, excludeId);

                if (excludeId > 0) {
                    ShotRecord rec = ShotHistoryStorage::loadShotRecordStatic(db, excludeId);
                    resolvedShot = ShotHistoryStorage::convertShotRecord(rec);
                }

                dialInSessions = DialingBlocks::buildDialInSessionsBlock(
                    db, kbId, excludeId, 5);
                if (resolvedShot.isValid()) {
                    bestRecentShot = DialingBlocks::buildBestRecentShotBlock(
                        db, kbId, excludeId, resolvedShot);
                    grinderContext = DialingBlocks::buildGrinderContextBlock(
                        db, resolvedShot.grinderModel,
                        resolvedShot.beverageType, resolvedShot.beanBrand);
                }
                if (!recentTurns.isEmpty() && !kbId.isEmpty()) {
                    DialingBlocks::RecentAdviceInputs in;
                    in.turns = recentTurns;
                    in.currentProfileKbId = kbId;
                    in.currentShotId = excludeId;
                    recentAdvice = DialingBlocks::buildRecentAdviceBlock(db, in);
                }
            });

            QMetaObject::invokeMethod(qApp,
                [self, systemPrompt, userPromptObj, recentShots = std::move(recentShots),
                 dialInSessions = std::move(dialInSessions),
                 bestRecentShot = std::move(bestRecentShot),
                 grinderContext = std::move(grinderContext),
                 recentAdvice = std::move(recentAdvice),
                 resolvedShot]() mutable {
                if (!self) return;

                // Merge the DB-derived blocks plus the main-thread SAW
                // block into the user prompt envelope. Empty blocks are
                // suppressed (no key, no null placeholder) to match
                // dialing_get_context's omission contract exactly. Single
                // source for the merge step — `ai_advisor_invoke` calls
                // the same primitive so the two surfaces cannot drift.
                self->enrichUserPromptObject(userPromptObj, resolvedShot,
                    dialInSessions, bestRecentShot, grinderContext, recentAdvice);

                QString finalUserPrompt = QString::fromUtf8(
                    QJsonDocument(userPromptObj).toJson(QJsonDocument::Indented));
                QString historyContext = ShotSummarizer::buildHistoryContext(recentShots);
                if (!historyContext.isEmpty()) {
                    finalUserPrompt += "\n\n" + historyContext;
                }
                self->m_conversation->ask(systemPrompt, finalUserPrompt);
            }, Qt::QueuedConnection);
        });
        connect(thread, &QThread::finished, thread, &QObject::deleteLater);
        thread->start();
    } else {
        // No shot history wired — fall back to the un-enriched envelope.
        QString userPrompt = QString::fromUtf8(
            QJsonDocument(userPromptObj).toJson(QJsonDocument::Indented));
        m_conversation->ask(systemPrompt, userPrompt);
    }
}

QString AIManager::generateEmailPrompt(ShotDataModel* shotData,
                                        Profile* profile,
                                        double doseWeight,
                                        double finalWeight,
                                        const QVariantMap& metadataMap)
{
    if (!shotData) {
        return "Error: No shot data available";
    }

    // Check beverage type — only espresso, filter, and pourover are supported
    if (profile) {
        QString bevType = profile->beverageType().toLower();
        if (bevType != "espresso" && bevType != "filter" && bevType != "pourover" && !bevType.isEmpty()) {
            return QString("AI analysis isn't available for %1 profiles yet — only espresso and filter are supported for now. Sorry about that!")
                .arg(profile->beverageType());
        }
    }

    // Extract metadata from QVariantMap
    ShotMetadata metadata = buildMetadata(
        metadataMap.value("beanBrand").toString(),
        metadataMap.value("beanType").toString(),
        metadataMap.value("roastDate").toString(),
        metadataMap.value("roastLevel").toString(),
        metadataMap.value("grinderBrand").toString(),
        metadataMap.value("grinderModel").toString(),
        metadataMap.value("grinderBurrs").toString(),
        metadataMap.value("grinderSetting").toString(),
        metadataMap.value("enjoymentScore").toInt(),
        metadataMap.value("tastingNotes").toString());
    ShotSummary summary = m_summarizer->summarize(shotData, profile, metadata, doseWeight, finalWeight);

    QString systemPrompt = ShotSummarizer::shotAnalysisSystemPrompt(
        summary.beverageType, summary.profileTitle, summary.profileType, summary.profileKbId);
    QString userPrompt = m_summarizer->buildUserPrompt(summary);

    // Note: dial-in history is omitted here because this method returns synchronously
    // and DB queries must not run on the main thread. The email prompt is a one-off export
    // so historical context is less critical than in interactive analysis.

    return systemPrompt + "\n\n---\n\n" + userPrompt +
           "\n\n---\n\nGenerated by Decenza. Paste into ChatGPT, Claude, or your preferred AI.";
}

QString AIManager::generateShotSummary(ShotDataModel* shotData,
                                        Profile* profile,
                                        double doseWeight,
                                        double finalWeight,
                                        const QVariantMap& metadataMap)
{
    if (!shotData) {
        return "Error: No shot data available";
    }

    // Extract metadata from QVariantMap
    ShotMetadata metadata = buildMetadata(
        metadataMap.value("beanBrand").toString(),
        metadataMap.value("beanType").toString(),
        metadataMap.value("roastDate").toString(),
        metadataMap.value("roastLevel").toString(),
        metadataMap.value("grinderBrand").toString(),
        metadataMap.value("grinderModel").toString(),
        metadataMap.value("grinderBurrs").toString(),
        metadataMap.value("grinderSetting").toString(),
        metadataMap.value("enjoymentScore").toInt(),
        metadataMap.value("tastingNotes").toString());
    ShotSummary summary = m_summarizer->summarize(shotData, profile, metadata, doseWeight, finalWeight);

    return m_summarizer->buildUserPrompt(summary);
}

QString AIManager::generateHistoryShotSummary(const ShotProjection& shotData)
{
    ShotSummary summary = m_summarizer->summarizeFromHistory(shotData);
    return m_summarizer->buildUserPrompt(summary);
}

QJsonObject AIManager::buildUserPromptObjectForShot(const ShotProjection& shotData)
{
    ShotSummary summary = m_summarizer->summarizeFromHistory(shotData);
    return m_summarizer->buildUserPromptObject(summary);
}

QString AIManager::buildShotAnalysisProseForShot(const ShotProjection& shotData)
{
    ShotSummary summary = m_summarizer->summarizeFromHistory(shotData);
    return m_summarizer->buildShotAnalysisProse(summary);
}

void AIManager::enrichUserPromptObject(QJsonObject& payload,
                                       const ShotProjection& shotData,
                                       const QJsonArray& dialInSessions,
                                       const QJsonObject& bestRecentShot,
                                       const QJsonObject& grinderContext,
                                       const QJsonArray& recentAdvice) const
{
    if (!dialInSessions.isEmpty())
        payload["dialInSessions"] = dialInSessions;
    if (!bestRecentShot.isEmpty())
        payload["bestRecentShot"] = bestRecentShot;
    if (!grinderContext.isEmpty())
        payload["grinderContext"] = grinderContext;
    // Closed-loop coaching: prior advisor turns paired with the user's
    // actual next shots (issue #1053). Empty array (no qualifying turns
    // yet) → key omitted; never `recentAdvice: []` placeholder.
    if (!recentAdvice.isEmpty())
        payload["recentAdvice"] = recentAdvice;
    if (shotData.isValid()) {
        const QJsonObject sawPrediction = DialingBlocks::buildSawPredictionBlock(
            m_settings, m_profileManager, shotData);
        if (!sawPrediction.isEmpty())
            payload["sawPrediction"] = sawPrediction;
    }
}

void AIManager::setShotHistoryStorage(ShotHistoryStorage* storage)
{
    m_shotHistory = storage;
}

// File-scope helper: runs on a background thread with its own SQLite connection.
// Returns (timestamp, fullShot) pairs. Extracted from requestRecentShotContext
// to reduce lambda nesting. NOT safe to call from the main thread (would conflict
// with the primary DB connection).
static QList<QPair<qint64, ShotProjection>> loadQualifiedShots(
    const QString& dbPath,
    const QString& beanBrand, const QString& beanType,
    const QString& profileName, int excludeShotId)
{
    QList<QPair<qint64, ShotProjection>> qualifiedShots;

    withTempDb(dbPath, "ai_context", [&](QSqlDatabase& db) {
        // 1. Look up the current shot's timestamp
        qint64 shotTimestamp = 0;
        {
            QSqlQuery q(db);
            q.prepare("SELECT timestamp FROM shots WHERE id = ?");
            q.bindValue(0, static_cast<qint64>(excludeShotId));
            if (!q.exec()) {
                qWarning() << "AIManager::requestRecentShotContext: timestamp query failed:" << q.lastError().text();
            } else if (q.next()) {
                shotTimestamp = q.value(0).toLongLong();
            } else {
                qDebug() << "AIManager::requestRecentShotContext: no shot found for excludeShotId=" << excludeShotId;
            }
        }

        if (shotTimestamp <= 0) return;

        // 2. Query candidates: same bean/profile, up to 3 weeks before this shot
        qint64 dateFrom = shotTimestamp - 21 * 24 * 3600;
        QStringList conditions;
        QVariantList bindValues;
        if (!beanBrand.isEmpty()) { conditions << "bean_brand = ?"; bindValues << beanBrand; }
        if (!beanType.isEmpty()) { conditions << "bean_type = ?"; bindValues << beanType; }
        if (!profileName.isEmpty()) { conditions << "profile_name = ?"; bindValues << profileName; }
        conditions << "timestamp >= ?" << "timestamp <= ?";
        bindValues << dateFrom << shotTimestamp;

        QString sql = "SELECT id, timestamp, profile_name, duration_seconds, final_weight "
                      "FROM shots WHERE " + conditions.join(" AND ") +
                      " ORDER BY timestamp DESC LIMIT 6";

        QSqlQuery q(db);
        q.prepare(sql);
        for (int i = 0; i < bindValues.size(); ++i)
            q.bindValue(i, bindValues[i]);

        struct Candidate { qint64 id; qint64 timestamp; QString profileName; double duration; double finalWeight; };
        QList<Candidate> candidates;
        if (q.exec()) {
            while (q.next()) {
                candidates.append({q.value(0).toLongLong(), q.value(1).toLongLong(),
                                   q.value(2).toString(), q.value(3).toDouble(), q.value(4).toDouble()});
            }
        } else {
            qWarning() << "AIManager::requestRecentShotContext: candidate query failed:" << q.lastError().text();
        }

        qDebug() << "AIManager::requestRecentShotContext: excludeShotId=" << excludeShotId
                 << "shotTimestamp=" << QDateTime::fromSecsSinceEpoch(shotTimestamp).toString("yyyy-MM-dd HH:mm")
                 << "filter: bean=" << beanBrand << beanType << "profile=" << profileName
                 << "candidates=" << candidates.size();

        // 3. Filter and load full records for up to 3 qualifying shots
        int included = 0;
        for (const auto& c : candidates) {
            if (included >= 3) break;

            if (c.id == excludeShotId) {
                qDebug() << "  Shot id=" << c.id << "-> SKIPPED (current shot)";
                continue;
            }

            // Lightweight mistake check (duration < 10s or weight < 5g)
            if (c.duration < 10.0 || c.finalWeight < 5.0) {
                qDebug() << "  Shot id=" << c.id << "-> SKIPPED (mistake)";
                continue;
            }

            ShotProjection fullShot;
            try {
                ShotRecord record = ShotHistoryStorage::loadShotRecordStatic(db, c.id);
                fullShot = ShotHistoryStorage::convertShotRecord(record);
            } catch (const std::exception& e) {
                qWarning() << "  Shot id=" << c.id << "-> SKIPPED (exception:" << e.what() << ")";
                continue;
            }
            if (!fullShot.isValid()) {
                qWarning() << "  Shot id=" << c.id << "-> SKIPPED (convertShotRecord returned empty)";
                continue;
            }

            // Check targetWeight-based mistake filter (needs full record)
            if (fullShot.targetWeightG > 0.0 && c.finalWeight < fullShot.targetWeightG / 3.0) {
                qDebug() << "  Shot id=" << c.id << "-> SKIPPED (mistake, weight < 1/3 target)";
                continue;
            }

            qDebug() << "  Shot id=" << c.id << "-> INCLUDED";
            qualifiedShots.append({c.timestamp, std::move(fullShot)});
            ++included;
        }
    });
    return qualifiedShots;
}

void AIManager::requestRecentShotContext(const QString& beanBrand, const QString& beanType, const QString& profileName, int excludeShotId)
{
    if (!m_shotHistory || (beanBrand.isEmpty() && profileName.isEmpty())) {
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
    QThread* thread = QThread::create([self, dbPath, beanBrand, beanType, profileName, excludeShotId, serial]() {
        auto qualifiedShots = loadQualifiedShots(dbPath, beanBrand, beanType, profileName, excludeShotId);

        // Query grinder context on background thread using the shared helper (also used by MCP dialing_get_context)
        GrinderContext grinderCtx;
        QString grinderBrand;
        withTempDb(dbPath, "ai_grinder_ctx", [&](QSqlDatabase& db) {
            QSqlQuery q(db);
            q.prepare("SELECT grinder_brand, grinder_model, beverage_type "
                      "FROM shots WHERE id = ?");
            q.bindValue(0, static_cast<qint64>(excludeShotId));
            if (q.exec() && q.next()) {
                grinderBrand = q.value(0).toString();
                QString model = q.value(1).toString();
                QString bev = q.value(2).toString();
                if (!model.isEmpty())
                    grinderCtx = ShotHistoryStorage::queryGrinderContext(db, model, bev);
            }
        });

        // Summarization runs on main thread (ShotSummarizer is owned by AIManager).
        // The render+emit work is in `emitRecentShotContext` so the
        // canonical-source separation logic can be exercised by tests
        // (`friend class tst_AIManager`) without standing up a real DB.
        QMetaObject::invokeMethod(qApp, [self, serial, qualifiedShots = std::move(qualifiedShots),
                                         grinderCtx = std::move(grinderCtx),
                                         grinderBrand = std::move(grinderBrand)]() mutable {
            if (!self) return;
            self->emitRecentShotContext(qualifiedShots, grinderCtx, grinderBrand, serial);
        }, Qt::QueuedConnection);
    });

    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void AIManager::emitRecentShotContext(
    const QList<QPair<qint64, ShotProjection>>& qualifiedShots,
    const GrinderContext& grinderCtx,
    const QString& grinderBrand,
    int serial)
{
    if (serial != m_contextSerial) {
        // Stale request superseded by a newer one — emit empty so QML clears contextLoading.
        emit recentShotContextReady(QString());
        return;
    }

    QString result;

    // Per openspec optimize-dialing-context-payload (task 10.3):
    // hoist profile + setup constants to a single header at the
    // top of the history section, then render each shot in
    // `HistoryBlock` mode so the per-shot blocks carry shot-
    // variable data only. Saves ~5,400 chars across a 4-shot
    // history (Northbound 80's Espresso baseline) by killing
    // N× repetition of profile intent + recipe + grinder/bean
    // identity.
    QString profileTitle, profileIntent, profileRecipe;
    QString setupGrinderBrand, setupGrinderModel, setupGrinderBurrs;
    QString setupBeanBrand, setupBeanType;
    // Empty fields read as "unrecorded, inherit" — not "different."
    // Older shots predating DYE recording have empty grinder/bean
    // strings; treating those as a mismatch would suppress the
    // hoisted Setup header for any history that mixes
    // pre-DYE shots with post-DYE shots. Only flip setupShared
    // false when both sides are non-empty AND differ. The shared
    // values are populated lazily via firstNonEmpty so a recorded
    // value seeds the canonical even if shot[0] was unrecorded.
    bool setupShared = !qualifiedShots.isEmpty();
    auto seedOrCompare = [&setupShared](QString& canonical, const QString& v) {
        if (canonical.isEmpty()) {
            canonical = v;
        } else if (!v.isEmpty() && v != canonical) {
            setupShared = false;
        }
    };
    for (const auto& qs : qualifiedShots) {
        const ShotProjection& s = qs.second;
        seedOrCompare(setupGrinderBrand, s.grinderBrand);
        seedOrCompare(setupGrinderModel, s.grinderModel);
        seedOrCompare(setupGrinderBurrs, s.grinderBurrs);
        seedOrCompare(setupBeanBrand, s.beanBrand);
        seedOrCompare(setupBeanType, s.beanType);
        if (profileTitle.isEmpty() && !s.profileName.isEmpty())
            profileTitle = s.profileName;
        if (profileIntent.isEmpty() && !s.profileNotes.isEmpty())
            profileIntent = s.profileNotes;
        if (profileRecipe.isEmpty() && !s.profileJson.isEmpty())
            profileRecipe = Profile::describeFramesFromJson(s.profileJson);
    }

    QStringList shotSections;
    for (const auto& qs : qualifiedShots) {
        ShotSummary summary = m_summarizer->summarizeFromHistory(qs.second);
        QString summaryText = m_summarizer->buildUserPrompt(
            summary, ShotSummarizer::RenderMode::HistoryBlock);
        if (summaryText.isEmpty()) continue;

        static const bool use12h = QLocale::system().timeFormat(QLocale::ShortFormat).contains("AP", Qt::CaseInsensitive);
        QString dateStr = QDateTime::fromSecsSinceEpoch(qs.first).toString(use12h ? "MMM d, h:mm AP" : "MMM d, HH:mm");
        shotSections.prepend(QString("### Shot (%1)\n\n%2").arg(dateStr).arg(summaryText));
    }

    if (!shotSections.isEmpty()) {
        result = "## Previous Shots with This Bean & Profile\n\n"
                 "All shots below use the same profile as the current shot. "
                 "Do not comment on frame-level recipe details unless they changed between shots. "
                 "Focus on what the user changed (grind, dose, temperature) and how it affected the outcome.\n\n";

        if (!profileTitle.isEmpty()) {
            result += "### Profile: " + profileTitle + "\n";
            if (!profileIntent.isEmpty())
                result += profileIntent + "\n";
            if (!profileRecipe.isEmpty())
                result += profileRecipe;
            result += "\n";
        }

        if (setupShared && (!setupGrinderBrand.isEmpty() || !setupGrinderModel.isEmpty()
                            || !setupBeanBrand.isEmpty() || !setupBeanType.isEmpty())) {
            // Build each segment as a complete fragment, then join with " "
            // — that way no segment owns a leading space, and absent fields
            // don't produce double-space artifacts (e.g. burrs without a
            // grinder brand+model used to render "### Setup:  with 63mm").
            QStringList parts;
            QString grinderName;
            if (!setupGrinderBrand.isEmpty()) grinderName = setupGrinderBrand;
            if (!setupGrinderModel.isEmpty()) {
                if (!grinderName.isEmpty()) grinderName += " ";
                grinderName += setupGrinderModel;
            }
            if (!setupGrinderBurrs.isEmpty()) {
                grinderName += grinderName.isEmpty()
                    ? setupGrinderBurrs
                    : " with " + setupGrinderBurrs;
            }
            if (!grinderName.isEmpty()) parts << grinderName;

            QString beanName;
            if (!setupBeanBrand.isEmpty() && !setupBeanType.isEmpty())
                beanName = setupBeanBrand + " - " + setupBeanType;
            else if (!setupBeanBrand.isEmpty())
                beanName = setupBeanBrand;
            else if (!setupBeanType.isEmpty())
                beanName = setupBeanType;
            if (!beanName.isEmpty())
                parts << (parts.isEmpty() ? beanName : "on " + beanName);

            result += "### Setup: " + parts.join(" ") + "\n\n";
        }

        result += shotSections.join("\n\n");
    }

    // Append grinder context if available (observed settings range and step size)
    if (!grinderCtx.settingsObserved.isEmpty()) {
        QString section = "\n\n## Grinder Context\n\n"
            "From the user's own shot history with this grinder:\n\n";
        section += "- **Model**: " + grinderCtx.model + "\n";

        // Burr specs are already shown per-shot in buildUserPrompt().
        // Only add swappability here — it's grinder-level info not in per-shot data.
        if (GrinderAliases::isBurrSwappable(grinderBrand, grinderCtx.model))
            section += "- **Burr-swappable**: yes (aftermarket burrs available for this grinder)\n";

        section += "- **Settings used for " + grinderCtx.beverageType + "**: "
                 + grinderCtx.settingsObserved.join(", ") + "\n";
        if (grinderCtx.allNumeric && grinderCtx.maxSetting > grinderCtx.minSetting) {
            section += "- **Range explored**: " + QString::number(grinderCtx.minSetting) + " \u2013 "
                     + QString::number(grinderCtx.maxSetting) + "\n";
            if (grinderCtx.smallestStep > 0) {
                section += "- **Smallest step**: " + QString::number(grinderCtx.smallestStep) + "\n";
            }
        }
        result += section;
    }

    emit recentShotContextReady(result);
}

void AIManager::testConnection()
{
    AIProvider* provider = currentProvider();
    if (!provider) {
        m_lastTestResult = "No AI provider selected";
        m_lastTestSuccess = false;
        emit testResultChanged();
        return;
    }

    provider->testConnection();
}

void AIManager::analyze(const QString& systemPrompt, const QString& userPrompt)
{
    if (m_analyzing) {
        m_lastError = "Analysis already in progress";
        emit errorOccurred(m_lastError);
        return;
    }

    AIProvider* provider = currentProvider();
    if (!provider) {
        m_lastError = "No AI provider configured";
        emit errorOccurred(m_lastError);
        return;
    }

    if (!isConfigured()) {
        m_lastError = "AI provider not configured";
        emit errorOccurred(m_lastError);
        return;
    }

    m_analyzing = true;
    m_isConversationRequest = false;
    emit analyzingChanged();

    // Store for logging
    m_lastSystemPrompt = systemPrompt;
    m_lastUserPrompt = userPrompt;

    logPrompt(selectedProvider(), systemPrompt, userPrompt);
    provider->analyze(systemPrompt, userPrompt);
}

void AIManager::analyzeConversation(const QString& systemPrompt, const QJsonArray& messages)
{
    if (m_analyzing) {
        emit conversationErrorOccurred("Analysis already in progress");
        return;
    }

    AIProvider* provider = currentProvider();
    if (!provider) {
        m_lastError = "No AI provider configured";
        emit conversationErrorOccurred(m_lastError);
        return;
    }

    if (!isConfigured()) {
        m_lastError = "AI provider not configured";
        emit conversationErrorOccurred(m_lastError);
        return;
    }

    m_analyzing = true;
    m_isConversationRequest = true;
    emit analyzingChanged();

    // Store for logging — flatten for the log file
    m_lastSystemPrompt = systemPrompt;
    m_lastUserPrompt = QString("[Conversation with %1 messages]").arg(messages.size());

    logPrompt(selectedProvider(), systemPrompt, m_lastUserPrompt);
    provider->analyzeConversation(systemPrompt, messages);
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
    if (m_isConversationRequest) {
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
    if (m_isConversationRequest) {
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
    }

    auto* anthropic = dynamic_cast<AnthropicProvider*>(m_anthropicProvider.get());
    if (anthropic) {
        anthropic->setApiKey(m_settings->ai()->anthropicApiKey());
    }

    auto* gemini = dynamic_cast<GeminiProvider*>(m_geminiProvider.get());
    if (gemini) {
        gemini->setApiKey(m_settings->ai()->geminiApiKey());
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

QJsonObject AIManager::ConversationEntry::toJson() const
{
    QJsonObject obj;
    obj["key"] = key;
    obj["beanBrand"] = beanBrand;
    obj["beanType"] = beanType;
    obj["profileName"] = profileName;
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
    entry.timestamp = obj["timestamp"].toVariant().toLongLong();
    return entry;
}

QString AIManager::conversationKey(const QString& beanBrand, const QString& beanType, const QString& profileName)
{
    QString normalized = beanBrand.toLower().trimmed() + "|" +
                         beanType.toLower().trimmed() + "|" +
                         profileName.toLower().trimmed();
    QByteArray hash = QCryptographicHash::hash(normalized.toUtf8(), QCryptographicHash::Sha1);
    return hash.toHex().left(16);
}

void AIManager::loadConversationIndex()
{
    QSettings settings;
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
    QSettings settings;
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
    QSettings settings;
    QString prefix = "ai/conversations/" + oldest.key + "/";
    settings.remove(prefix + "systemPrompt");
    settings.remove(prefix + "messages");
    settings.remove(prefix + "timestamp");

    qDebug() << "AIManager: Evicted oldest conversation:" << oldest.beanBrand << oldest.beanType << oldest.profileName;
    saveConversationIndex();
}

void AIManager::migrateFromLegacyConversation()
{
    QSettings settings;

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

QString AIManager::switchConversation(const QString& beanBrand, const QString& beanType, const QString& profileName)
{
    QString key = conversationKey(beanBrand, beanType, profileName);

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

    // Set new storage key and load if exists
    m_conversation->setStorageKey(key);
    m_conversation->setContextLabel(beanBrand, beanType, profileName);

    if (exists) {
        m_conversation->loadFromStorage();
        touchConversationEntry(key);
    } else {
        // Evict oldest if at capacity
        evictOldestConversation();

        // Add new entry to front of index
        ConversationEntry newEntry;
        newEntry.key = key;
        newEntry.beanBrand = beanBrand;
        newEntry.beanType = beanType;
        newEntry.profileName = profileName;
        newEntry.timestamp = QDateTime::currentSecsSinceEpoch();
        m_conversationIndex.prepend(newEntry);
        saveConversationIndex();
    }

    emit m_conversation->savedConversationChanged();
    qDebug() << "AIManager: Switched to conversation key:" << key
             << "(" << beanBrand << beanType << "/" << profileName << ")";
    return key;
}

void AIManager::loadMostRecentConversation()
{
    if (m_conversationIndex.isEmpty()) {
        m_conversation->setStorageKey(QString());
        m_conversation->setContextLabel(QString(), QString(), QString());
        return;
    }

    const auto& entry = m_conversationIndex.first();
    m_conversation->setStorageKey(entry.key);
    m_conversation->setContextLabel(entry.beanBrand, entry.beanType, entry.profileName);
    m_conversation->loadFromStorage();
    qDebug() << "AIManager: Loaded most recent conversation:" << entry.key
             << "(" << entry.beanBrand << entry.beanType << "/" << entry.profileName << ")";
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

bool AIManager::isMistakeShot(const ShotProjection& shotData) const
{
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
