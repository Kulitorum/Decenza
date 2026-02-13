#include "aimanager.h"
#include "aiprovider.h"
#include "aiconversation.h"
#include "shotsummarizer.h"
#include "../core/settings.h"
#include "../models/shotdatamodel.h"
#include "../profile/profile.h"
#include "../network/visualizeruploader.h"

#include <QNetworkAccessManager>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QDateTime>
#include <QDebug>
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

    // Load any saved conversation from previous session
    if (m_conversation->hasSavedConversation()) {
        m_conversation->loadFromStorage();
        qDebug() << "AIManager: Loaded saved conversation with" << m_conversation->messageCount() << "messages";
    }

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
    QString ollamaEndpoint = m_settings->value("ai/ollamaEndpoint", "http://localhost:11434").toString();
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
    AIProvider* provider = nullptr;
    if (providerId == "openai") provider = m_openaiProvider.get();
    else if (providerId == "anthropic") provider = m_anthropicProvider.get();
    else if (providerId == "gemini") provider = m_geminiProvider.get();
    else if (providerId == "openrouter") provider = m_openrouterProvider.get();
    else if (providerId == "ollama") provider = m_ollamaProvider.get();
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

AIProvider* AIManager::currentProvider() const
{
    QString providerId = selectedProvider();

    if (providerId == "openai") return m_openaiProvider.get();
    if (providerId == "anthropic") return m_anthropicProvider.get();
    if (providerId == "gemini") return m_geminiProvider.get();
    if (providerId == "openrouter") return m_openrouterProvider.get();
    if (providerId == "ollama") return m_ollamaProvider.get();

    return m_openaiProvider.get();  // Default
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
    QString prompt;
    QTextStream out(&prompt);

    // === Helper lambdas ===

    // Find value at a given time via linear interpolation
    auto findValueAtTime = [](const QVariantList& data, double targetTime) -> double {
        if (data.isEmpty()) return 0;
        for (int i = 0; i < data.size(); i++) {
            QVariantMap p = data[i].toMap();
            double t = p.value("x", 0.0).toDouble();
            if (t >= targetTime) {
                if (i == 0) return p.value("y", 0.0).toDouble();
                QVariantMap prev = data[i - 1].toMap();
                double t0 = prev.value("x", 0.0).toDouble();
                double y0 = prev.value("y", 0.0).toDouble();
                double y1 = p.value("y", 0.0).toDouble();
                double frac = (t - t0 > 0.001) ? (targetTime - t0) / (t - t0) : 0;
                return y0 + frac * (y1 - y0);
            }
        }
        return data.last().toMap().value("y", 0.0).toDouble();
    };

    // Detect channeling: flow spike >50% during extraction
    auto detectChanneling = [](const QVariantList& flowData, double afterTime) -> bool {
        if (flowData.size() < 10) return false;
        for (int i = 5; i < flowData.size() - 5; i++) {
            QVariantMap curr = flowData[i].toMap();
            if (curr.value("x", 0.0).toDouble() < afterTime) continue;
            QVariantMap prev = flowData[i - 5].toMap();
            double prevFlow = prev.value("y", 0.0).toDouble();
            double currFlow = curr.value("y", 0.0).toDouble();
            if (prevFlow > 0.5 && currFlow > prevFlow * 1.5) return true;
        }
        return false;
    };

    // Detect temperature instability: avg deviation from goal >2°C
    auto detectTempInstability = [&findValueAtTime](const QVariantList& tempData, const QVariantList& tempGoal) -> bool {
        if (tempData.isEmpty() || tempGoal.isEmpty()) return false;
        double deviationSum = 0;
        int count = 0;
        for (const QVariant& point : tempData) {
            QVariantMap p = point.toMap();
            double t = p.value("x", 0.0).toDouble();
            double actual = p.value("y", 0.0).toDouble();
            double target = findValueAtTime(tempGoal, t);
            if (target > 0) {
                deviationSum += std::abs(actual - target);
                count++;
            }
        }
        return count > 0 && (deviationSum / count) > 2.0;
    };

    // === Shot Summary ===
    out << "## Shot Summary\n\n";
    QString beverageType = shotData.value("beverageType", "espresso").toString();
    out << "- **Beverage type**: " << beverageType << "\n";
    out << "- **Profile**: " << shotData.value("profileName", "Unknown").toString() << "\n";

    double doseWeight = shotData.value("doseWeight", 0.0).toDouble();
    double finalWeight = shotData.value("finalWeight", 0.0).toDouble();
    double ratio = doseWeight > 0 ? finalWeight / doseWeight : 0;

    out << "- **Dose**: " << QString::number(doseWeight, 'f', 1) << "g -> ";
    out << "**Yield**: " << QString::number(finalWeight, 'f', 1) << "g ";
    out << "(ratio 1:" << QString::number(ratio, 'f', 1) << ")\n";
    out << "- **Duration**: " << QString::number(shotData.value("duration", 0.0).toDouble(), 'f', 0) << "s\n";

    // Coffee info
    QString beanBrand = shotData.value("beanBrand").toString();
    QString beanType = shotData.value("beanType").toString();
    QString roastLevel = shotData.value("roastLevel").toString();
    QString grinderModel = shotData.value("grinderModel").toString();
    QString grinderSetting = shotData.value("grinderSetting").toString();

    if (!beanBrand.isEmpty() || !beanType.isEmpty()) {
        out << "- **Coffee**: " << beanBrand;
        if (!beanBrand.isEmpty() && !beanType.isEmpty()) out << " - ";
        out << beanType;
        if (!roastLevel.isEmpty()) out << " (" << roastLevel << ")";
        out << "\n";
    }
    if (!grinderModel.isEmpty()) {
        out << "- **Grinder**: " << grinderModel;
        if (!grinderSetting.isEmpty()) out << " @ " << grinderSetting;
        out << "\n";
    }
    QString beanNotes = shotData.value("beanNotes").toString();
    if (!beanNotes.isEmpty())
        out << "- **Bean notes**: " << beanNotes << "\n";

    // Profile description (prominently labeled so AI understands intent)
    QString profileNotes = shotData.value("profileNotes").toString();
    if (!profileNotes.isEmpty())
        out << "- **Profile description**: " << profileNotes << "\n";
    out << "\n";

    // TDS/EY
    double drinkTds = shotData.value("drinkTds", 0.0).toDouble();
    double drinkEy = shotData.value("drinkEy", 0.0).toDouble();
    if (drinkTds > 0 || drinkEy > 0) {
        out << "- **Extraction**: ";
        if (drinkTds > 0) out << "TDS " << QString::number(drinkTds, 'f', 2) << "%";
        if (drinkTds > 0 && drinkEy > 0) out << ", ";
        if (drinkEy > 0) out << "EY " << QString::number(drinkEy, 'f', 1) << "%";
        out << "\n\n";
    }

    // === Profile Recipe ===
    QString profileJson = shotData.value("profileJson").toString();
    if (!profileJson.isEmpty()) {
        QString recipe = Profile::describeFramesFromJson(profileJson);
        if (!recipe.isEmpty()) {
            out << recipe << "\n";
        }
    }

    // === Phase Data ===
    QVariantList pressureData = shotData.value("pressure").toList();
    QVariantList flowData = shotData.value("flow").toList();
    QVariantList tempData = shotData.value("temperature").toList();
    QVariantList weightData = shotData.value("weight").toList();
    QVariantList pressureGoal = shotData.value("pressureGoal").toList();
    QVariantList flowGoal = shotData.value("flowGoal").toList();
    QVariantList tempGoal = shotData.value("temperatureGoal").toList();
    QVariantList phases = shotData.value("phases").toList();
    double duration = shotData.value("duration", 60.0).toDouble();

    out << "## Phase Data\n\n";
    out << "Each phase shows samples at start, middle, and end. Values: actual (target).\n\n";

    // Build phase list — fall back to single "Extraction" phase if no markers
    struct PhaseInfo {
        QString label;
        double startTime;
        double endTime;
        bool isFlowMode;
    };
    QList<PhaseInfo> phaseList;

    if (!phases.isEmpty()) {
        for (int i = 0; i < phases.size(); i++) {
            QVariantMap marker = phases[i].toMap();
            double startTime = marker.value("time", 0.0).toDouble();
            double endTime = (i + 1 < phases.size())
                ? phases[i + 1].toMap().value("time", 0.0).toDouble()
                : duration;
            if (endTime <= startTime) continue;
            phaseList.append({
                marker.value("label", "Phase").toString(),
                startTime,
                endTime,
                marker.value("isFlowMode", false).toBool()
            });
        }
    }

    if (phaseList.isEmpty()) {
        // Fallback: single "Extraction" phase spanning the whole shot
        phaseList.append({"Extraction", 0.0, duration, false});
    }

    // Determine extraction start time for channeling detection
    double extractionStartTime = 3.0;  // fallback
    for (const auto& ph : phaseList) {
        QString lower = ph.label.toLower();
        if (lower.contains("extract") || lower.contains("pour") ||
            lower.contains("hold") || lower == "main") {
            extractionStartTime = ph.startTime;
            break;
        }
    }

    // Output each phase
    for (const auto& ph : phaseList) {
        QString controlMode = ph.isFlowMode
            ? "FLOW-CONTROLLED - pressure is just a result of resistance"
            : "PRESSURE-CONTROLLED - flow is just a result of resistance";

        out << "### " << ph.label << " (" << QString::number(ph.endTime - ph.startTime, 'f', 0) << "s) - " << controlMode << "\n";

        double times[3] = { ph.startTime, (ph.startTime + ph.endTime) / 2, ph.endTime - 0.1 };
        const char* labels[3] = { "Start", "Middle", "End" };

        for (int i = 0; i < 3; i++) {
            double t = times[i];
            double pressure = findValueAtTime(pressureData, t);
            double flow = findValueAtTime(flowData, t);
            double temp = findValueAtTime(tempData, t);
            double weight = findValueAtTime(weightData, t);
            double pTarget = findValueAtTime(pressureGoal, t);
            double fTarget = findValueAtTime(flowGoal, t);
            double tTarget = findValueAtTime(tempGoal, t);

            out << "- **" << labels[i] << "** @" << QString::number(t, 'f', 0) << "s: ";
            out << QString::number(pressure, 'f', 1);
            if (pTarget > 0.1) out << "(" << QString::number(pTarget, 'f', 0) << ")";
            out << " bar, ";
            out << QString::number(flow, 'f', 1);
            if (fTarget > 0.1) out << "(" << QString::number(fTarget, 'f', 1) << ")";
            out << " ml/s, ";
            out << QString::number(temp, 'f', 0);
            if (tTarget > 0) out << "(" << QString::number(tTarget, 'f', 0) << ")";
            out << "\u00B0C, ";
            out << QString::number(weight, 'f', 1) << "g\n";
        }
        out << "\n";
    }

    // === Tasting Feedback ===
    out << "## Tasting Feedback\n\n";
    int enjoyment = shotData.value("enjoyment", 0).toInt();
    QString notes = shotData.value("espressoNotes").toString();

    if (enjoyment > 0) {
        out << "- **Score**: " << enjoyment << "/100";
        if (enjoyment >= 80) out << " - Good shot!";
        else if (enjoyment >= 60) out << " - Decent, room for improvement";
        else if (enjoyment >= 40) out << " - Needs work";
        else out << " - Problematic";
        out << "\n";
    }
    if (!notes.isEmpty()) {
        out << "- **Notes**: \"" << notes << "\"\n";
    }
    if (enjoyment == 0 && notes.isEmpty()) {
        out << "- No tasting feedback provided\n";
    }
    out << "\n";

    // === Observations (anomaly detection) ===
    bool channeling = detectChanneling(flowData, extractionStartTime);
    bool tempUnstable = detectTempInstability(tempData, tempGoal);

    if (channeling || tempUnstable) {
        out << "## Observations\n\n";
        if (channeling)
            out << "- **Channeling detected**: Sudden flow spike (>50% increase) during extraction\n";
        if (tempUnstable)
            out << "- **Temperature unstable**: Average deviation from target exceeds 2\u00B0C\n";
        out << "\n";
    }

    out << "Analyze the curve data and sensory feedback. Provide ONE specific, evidence-based recommendation.\n";

    return prompt;
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
    emit analyzingChanged();

    // Store for logging
    m_lastSystemPrompt = systemPrompt;
    m_lastUserPrompt = userPrompt;

    logPrompt(selectedProvider(), systemPrompt, userPrompt);
    provider->analyze(systemPrompt, userPrompt);
}

void AIManager::analyzeConversation(const QString& systemPrompt, const QJsonArray& messages)
{
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
    emit recommendationReceived(response);
}

void AIManager::onAnalysisFailed(const QString& error)
{
    m_analyzing = false;
    m_lastError = error;

    // Log the failed response
    logResponse(selectedProvider(), error, false);

    emit analyzingChanged();
    emit errorOccurred(error);
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
        ollama->setEndpoint(m_settings->value("ai/ollamaEndpoint", "http://localhost:11434").toString());
        ollama->setModel(m_settings->value("ai/ollamaModel").toString());
    }

    emit configurationChanged();
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
    }
}
