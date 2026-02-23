#include "aimanager.h"
#include "aiprovider.h"
#include "aiconversation.h"
#include "shotsummarizer.h"
#include "../core/settings.h"
#include "../models/shotdatamodel.h"
#include "../profile/profile.h"
#include "../network/visualizeruploader.h"
#include "../history/shothistorystorage.h"

#include <QNetworkAccessManager>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QDateTime>
#include <QDebug>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonParseError>
#include <cmath>

AIManager::AIManager(Settings* settings, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_networkManager(new QNetworkAccessManager(this))
    , m_summarizer(std::make_unique<ShotSummarizer>(this))
{
    createProviders();

    // Create conversation handler for multi-turn interactions
    m_conversation = new AIConversation(this, this);

    // Migrate legacy single-conversation storage if needed
    migrateFromLegacyConversation();

    // Load conversation index and restore most recent conversation
    loadConversationIndex();
    loadMostRecentConversation();

    // Connect to settings changes
    connect(m_settings, &Settings::valueChanged, this, &AIManager::onSettingsChanged);
}

AIManager::~AIManager() = default;

void AIManager::createProviders()
{
    // Create OpenAI provider
    QString openaiKey = m_settings->value("ai/openaiKey").toString();
    auto* openai = new OpenAIProvider(m_networkManager, openaiKey, this);
    connect(openai, &AIProvider::analysisComplete, this, &AIManager::onAnalysisComplete);
    connect(openai, &AIProvider::analysisFailed, this, &AIManager::onAnalysisFailed);
    connect(openai, &AIProvider::testResult, this, &AIManager::onTestResult);
    m_openaiProvider.reset(openai);

    // Create Anthropic provider
    QString anthropicKey = m_settings->value("ai/anthropicKey").toString();
    auto* anthropic = new AnthropicProvider(m_networkManager, anthropicKey, this);
    connect(anthropic, &AIProvider::analysisComplete, this, &AIManager::onAnalysisComplete);
    connect(anthropic, &AIProvider::analysisFailed, this, &AIManager::onAnalysisFailed);
    connect(anthropic, &AIProvider::testResult, this, &AIManager::onTestResult);
    m_anthropicProvider.reset(anthropic);

    // Create Gemini provider
    QString geminiKey = m_settings->value("ai/geminiKey").toString();
    auto* gemini = new GeminiProvider(m_networkManager, geminiKey, this);
    connect(gemini, &AIProvider::analysisComplete, this, &AIManager::onAnalysisComplete);
    connect(gemini, &AIProvider::analysisFailed, this, &AIManager::onAnalysisFailed);
    connect(gemini, &AIProvider::testResult, this, &AIManager::onTestResult);
    m_geminiProvider.reset(gemini);

    // Create OpenRouter provider
    QString openrouterKey = m_settings->value("ai/openrouterKey").toString();
    QString openrouterModel = m_settings->value("ai/openrouterModel", "anthropic/claude-sonnet-4").toString();
    auto* openrouter = new OpenRouterProvider(m_networkManager, openrouterKey, openrouterModel, this);
    connect(openrouter, &AIProvider::analysisComplete, this, &AIManager::onAnalysisComplete);
    connect(openrouter, &AIProvider::analysisFailed, this, &AIManager::onAnalysisFailed);
    connect(openrouter, &AIProvider::testResult, this, &AIManager::onTestResult);
    m_openrouterProvider.reset(openrouter);

    // Create Ollama provider
    QString ollamaEndpoint = m_settings->value("ai/ollamaEndpoint", "").toString();
    QString ollamaModel = m_settings->value("ai/ollamaModel").toString();
    auto* ollama = new OllamaProvider(m_networkManager, ollamaEndpoint, ollamaModel, this);
    connect(ollama, &AIProvider::analysisComplete, this, &AIManager::onAnalysisComplete);
    connect(ollama, &AIProvider::analysisFailed, this, &AIManager::onAnalysisFailed);
    connect(ollama, &AIProvider::testResult, this, &AIManager::onTestResult);
    connect(ollama, &OllamaProvider::modelsRefreshed, this, &AIManager::onOllamaModelsRefreshed);
    m_ollamaProvider.reset(ollama);
}

QString AIManager::selectedProvider() const
{
    return m_settings->value("ai/provider", "openai").toString();
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
        m_settings->setValue("ai/provider", provider);
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

ShotMetadata AIManager::buildMetadata(const QString& beanBrand,
                                       const QString& beanType,
                                       const QString& roastDate,
                                       const QString& roastLevel,
                                       const QString& grinderModel,
                                       const QString& grinderSetting,
                                       int enjoymentScore,
                                       const QString& tastingNotes) const
{
    ShotMetadata metadata;
    metadata.beanBrand = beanBrand;
    metadata.beanType = beanType;
    metadata.roastDate = roastDate;
    metadata.roastLevel = roastLevel;
    metadata.grinderModel = grinderModel;
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
        metadata.value("grinderModel").toString(),
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
                             const QString& grinderModel,
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

    // Build metadata and summarize shot
    ShotMetadata metadata = buildMetadata(beanBrand, beanType, roastDate, roastLevel,
                                          grinderModel, grinderSetting, enjoymentScore, tastingNotes);
    ShotSummary summary = m_summarizer->summarize(shotData, profile, metadata, doseWeight, finalWeight);

    // Build prompts (select system prompt based on beverage type)
    QString systemPrompt = ShotSummarizer::systemPrompt(summary.beverageType);
    QString userPrompt = m_summarizer->buildUserPrompt(summary);

    // Use conversation to track history for follow-ups
    // This sets m_analyzing via analyze() and enables follow-up questions
    m_conversation->ask(systemPrompt, userPrompt);
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
        metadataMap.value("grinderModel").toString(),
        metadataMap.value("grinderSetting").toString(),
        metadataMap.value("enjoymentScore").toInt(),
        metadataMap.value("tastingNotes").toString());
    ShotSummary summary = m_summarizer->summarize(shotData, profile, metadata, doseWeight, finalWeight);

    QString systemPrompt = ShotSummarizer::systemPrompt(summary.beverageType);
    QString userPrompt = m_summarizer->buildUserPrompt(summary);

    return systemPrompt + "\n\n---\n\n" + userPrompt +
           "\n\n---\n\nGenerated by Decenza DE1. Paste into ChatGPT, Claude, or your preferred AI.";
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
        metadataMap.value("grinderModel").toString(),
        metadataMap.value("grinderSetting").toString(),
        metadataMap.value("enjoymentScore").toInt(),
        metadataMap.value("tastingNotes").toString());
    ShotSummary summary = m_summarizer->summarize(shotData, profile, metadata, doseWeight, finalWeight);

    return m_summarizer->buildUserPrompt(summary);
}

QString AIManager::generateHistoryShotSummary(const QVariantMap& shotData)
{
    ShotSummary summary = m_summarizer->summarizeFromHistory(shotData);
    return m_summarizer->buildUserPrompt(summary);
}

void AIManager::setShotHistoryStorage(ShotHistoryStorage* storage)
{
    m_shotHistory = storage;
}

QString AIManager::getRecentShotContext(const QString& beanBrand, const QString& beanType, const QString& profileName, int excludeShotId)
{
    if (!m_shotHistory || (beanBrand.isEmpty() && profileName.isEmpty()))
        return QString();

    // Look up the current shot's timestamp so we only include older shots
    qint64 shotTimestamp = m_shotHistory->getShotTimestamp(excludeShotId);
    if (shotTimestamp <= 0)
        return QString();

    // Build filter: match on non-empty fields, up to 3 weeks before this shot
    QVariantMap filter;
    if (!beanBrand.isEmpty()) filter["beanBrand"] = beanBrand;
    if (!beanType.isEmpty()) filter["beanType"] = beanType;
    if (!profileName.isEmpty()) filter["profileName"] = profileName;
    filter["dateFrom"] = shotTimestamp - 21 * 24 * 3600;
    filter["dateTo"] = shotTimestamp;  // Only shots before (or at) this shot's time

    // Fetch extra to have room after filtering out excludeShotId and mistakes
    QVariantList candidates = m_shotHistory->getShotsFiltered(filter, 0, 6);

    qDebug() << "AIManager::getRecentShotContext: excludeShotId=" << excludeShotId
             << "shotTimestamp=" << QDateTime::fromSecsSinceEpoch(shotTimestamp).toString("yyyy-MM-dd HH:mm")
             << "filter: bean=" << beanBrand << beanType << "profile=" << profileName
             << "candidates=" << candidates.size();

    QStringList shotSections;
    for (const QVariant& v : candidates) {
        if (shotSections.size() >= 3) break;

        QVariantMap shot = v.toMap();
        qint64 id = shot.value("id").toLongLong();
        qint64 timestamp = shot.value("timestamp").toLongLong();
        QString shotProfile = shot.value("profileName").toString();
        QString shotDate = QDateTime::fromSecsSinceEpoch(timestamp).toString("yyyy-MM-dd HH:mm");

        if (id == excludeShotId) {
            qDebug() << "  Shot id=" << id << "date=" << shotDate << "profile=" << shotProfile << "-> SKIPPED (current shot)";
            continue;
        }
        if (isMistakeShot(shot)) {
            qDebug() << "  Shot id=" << id << "date=" << shotDate << "profile=" << shotProfile << "-> SKIPPED (mistake)";
            continue;
        }

        qDebug() << "  Shot id=" << id << "date=" << shotDate << "profile=" << shotProfile << "-> INCLUDED";

        // Load full shot data (with time-series) for rich summary
        QVariantMap fullShot = m_shotHistory->getShot(id);
        if (fullShot.isEmpty()) continue;

        QString summary = generateHistoryShotSummary(fullShot);
        if (summary.isEmpty()) continue;

        // Format date for header
        QString dateStr = QDateTime::fromSecsSinceEpoch(timestamp).toString("MMM d, HH:mm");

        shotSections.prepend(QString("### Shot (%1)\n\n%2").arg(dateStr).arg(summary));
    }

    if (shotSections.isEmpty())
        return QString();

    return "## Previous Shots with This Bean & Profile\n\n" + shotSections.join("\n\n");
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
        openai->setApiKey(m_settings->value("ai/openaiKey").toString());
    }

    auto* anthropic = dynamic_cast<AnthropicProvider*>(m_anthropicProvider.get());
    if (anthropic) {
        anthropic->setApiKey(m_settings->value("ai/anthropicKey").toString());
    }

    auto* gemini = dynamic_cast<GeminiProvider*>(m_geminiProvider.get());
    if (gemini) {
        gemini->setApiKey(m_settings->value("ai/geminiKey").toString());
    }

    auto* openrouter = dynamic_cast<OpenRouterProvider*>(m_openrouterProvider.get());
    if (openrouter) {
        openrouter->setApiKey(m_settings->value("ai/openrouterKey").toString());
        openrouter->setModel(m_settings->value("ai/openrouterModel", "anthropic/claude-sonnet-4").toString());
    }

    auto* ollama = dynamic_cast<OllamaProvider*>(m_ollamaProvider.get());
    if (ollama) {
        ollama->setEndpoint(m_settings->value("ai/ollamaEndpoint", "").toString());
        ollama->setModel(m_settings->value("ai/ollamaModel").toString());
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

bool AIManager::isMistakeShot(const QVariantMap& shotData) const
{
    double duration = shotData.value("duration", 0.0).toDouble();
    double finalWeight = shotData.value("finalWeight", 0.0).toDouble();
    double targetWeight = shotData.value("targetWeight", 0.0).toDouble();

    if (duration < 10.0) return true;
    if (finalWeight < 5.0) return true;
    if (targetWeight > 0.0 && finalWeight < targetWeight / 3.0) return true;

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
