#include "aiconversation.h"
#include "aimanager.h"

#include <QDebug>
#include <QSettings>
#include <QJsonDocument>

AIConversation::AIConversation(AIManager* aiManager, QObject* parent)
    : QObject(parent)
    , m_aiManager(aiManager)
{
    // Connect to AIManager signals
    if (m_aiManager) {
        connect(m_aiManager, &AIManager::recommendationReceived,
                this, &AIConversation::onAnalysisComplete);
        connect(m_aiManager, &AIManager::errorOccurred,
                this, &AIConversation::onAnalysisFailed);
        connect(m_aiManager, &AIManager::providerChanged,
                this, &AIConversation::providerChanged);
    }
}

QString AIConversation::providerName() const
{
    if (!m_aiManager) return "AI";

    QString provider = m_aiManager->selectedProvider();
    if (provider == "openai") return "GPT";
    if (provider == "anthropic") return "Claude";
    if (provider == "gemini") return "Gemini";
    if (provider == "ollama") return "Ollama";
    return "AI";
}

void AIConversation::ask(const QString& systemPrompt, const QString& userMessage)
{
    if (m_busy || !m_aiManager) return;

    // Clear previous conversation and start fresh
    m_messages = QJsonArray();
    m_systemPrompt = systemPrompt;
    m_lastResponse.clear();
    m_errorMessage.clear();

    addUserMessage(userMessage);
    sendRequest();

    emit historyChanged();
}

void AIConversation::followUp(const QString& userMessage)
{
    if (m_busy || !m_aiManager) return;
    if (m_systemPrompt.isEmpty()) {
        qWarning() << "AIConversation::followUp called without prior ask()";
        return;
    }

    m_errorMessage.clear();
    addUserMessage(userMessage);
    sendRequest();

    emit historyChanged();
}

void AIConversation::clearHistory()
{
    m_messages = QJsonArray();
    m_systemPrompt.clear();
    m_lastResponse.clear();
    m_errorMessage.clear();

    emit historyChanged();
    qDebug() << "AIConversation: History cleared";
}

void AIConversation::addUserMessage(const QString& message)
{
    QJsonObject msg;
    msg["role"] = "user";
    msg["content"] = message;
    m_messages.append(msg);
}

void AIConversation::addAssistantMessage(const QString& message)
{
    QJsonObject msg;
    msg["role"] = "assistant";
    msg["content"] = message;
    m_messages.append(msg);
}

void AIConversation::sendRequest()
{
    if (!m_aiManager || !m_aiManager->isConfigured()) {
        m_errorMessage = "AI not configured";
        emit errorOccurred(m_errorMessage);
        return;
    }

    m_busy = true;
    emit busyChanged();

    // Build the full prompt with conversation history
    // For now, we concatenate messages into a single user prompt
    // since the current AIManager::analyze() doesn't support message arrays
    QString fullPrompt;

    for (int i = 0; i < m_messages.size(); i++) {
        QJsonObject msg = m_messages[i].toObject();
        QString role = msg["role"].toString();
        QString content = msg["content"].toString();

        if (role == "user") {
            if (i > 0) fullPrompt += "\n\n[User follow-up]:\n";
            fullPrompt += content;
        } else if (role == "assistant") {
            fullPrompt += "\n\n[Your previous response]:\n" + content;
        }
    }

    qDebug() << "AIConversation: Sending request with" << m_messages.size() << "messages";
    m_aiManager->analyze(m_systemPrompt, fullPrompt);
}

void AIConversation::onAnalysisComplete(const QString& response)
{
    if (!m_busy) return;  // Not our request

    m_busy = false;
    m_lastResponse = response;

    // Add assistant response to history
    addAssistantMessage(response);

    // Auto-save so conversation can be continued later
    saveToStorage();

    emit busyChanged();
    emit historyChanged();
    emit responseReceived(response);

    qDebug() << "AIConversation: Response received, history now has" << m_messages.size() << "messages";
}

void AIConversation::onAnalysisFailed(const QString& error)
{
    if (!m_busy) return;  // Not our request

    m_busy = false;
    m_errorMessage = error;

    // Remove the last user message since it failed
    if (!m_messages.isEmpty()) {
        m_messages.removeLast();
    }

    emit busyChanged();
    emit historyChanged();
    emit errorOccurred(error);

    qDebug() << "AIConversation: Request failed:" << error;
}

QString AIConversation::getConversationText() const
{
    QString text;

    for (int i = 0; i < m_messages.size(); i++) {
        QJsonObject msg = m_messages[i].toObject();
        QString role = msg["role"].toString();
        QString content = msg["content"].toString();

        if (i > 0) text += "\n\n---\n\n";

        if (role == "user") {
            // Check if this is a shot data message
            if (content.contains("Shot Summary") || content.contains("Here's my latest shot")) {
                // Find the user's question after the shot data
                // Format is: "Here's my latest shot:\n\n<shot summary>\n\n<user question>"
                QString userQuestion;

                // The shot summary contains structured data with lines like "Key: Value"
                // Find where the shot data ends and user's question begins
                // Look for the last double newline that separates shot data from question
                int shotStart = content.indexOf("Here's my latest shot:");
                if (shotStart >= 0) {
                    // Skip past "Here's my latest shot:\n\n"
                    int dataStart = content.indexOf("\n\n", shotStart);
                    if (dataStart >= 0) {
                        dataStart += 2;
                        // Find the end of shot data (look for pattern break)
                        // Shot data lines have format "Key: Value" or are part of structured sections
                        // The user's question is free-form text after the data

                        // Simple heuristic: find last "\n\n" and check if what follows
                        // looks like a question (doesn't contain ":" in typical key: value pattern)
                        int lastBreak = content.lastIndexOf("\n\n");
                        if (lastBreak > dataStart) {
                            QString afterBreak = content.mid(lastBreak + 2).trimmed();
                            // If it doesn't look like shot data (no "Key:" pattern at start)
                            if (!afterBreak.isEmpty() && !afterBreak.contains(": ") && afterBreak.length() < 500) {
                                userQuestion = afterBreak;
                            } else if (!afterBreak.isEmpty() && afterBreak.length() < 200) {
                                // Short text is likely a question
                                userQuestion = afterBreak;
                            }
                        }
                    }
                }

                // Format: [Shot Data] or [Coffee Data] depending on beverage type
                bool isFilter = content.contains("Beverage type**: filter", Qt::CaseInsensitive) ||
                               content.contains("Beverage type**: pourover", Qt::CaseInsensitive);
                text += isFilter ? "**[Coffee Data]**" : "**[Shot Data]**";
                if (!userQuestion.isEmpty()) {
                    text += "\n**You:** " + userQuestion;
                }
            } else {
                text += "**You:** " + content;
            }
        } else if (role == "assistant") {
            text += "**" + providerName() + ":** " + content;
        }
    }

    return text;
}

void AIConversation::addShotContext(const QString& shotSummary, const QString& beverageType)
{
    if (m_busy) return;

    // If no existing conversation, set up the system prompt based on beverage type
    if (m_systemPrompt.isEmpty()) {
        if (beverageType.toLower() == "filter" || beverageType.toLower() == "pourover") {
            m_systemPrompt = "You are an expert filter coffee consultant helping a user optimise brews made on a Decent DE1 profiling machine over multiple attempts. "
                             "Key principles: Taste is king — numbers serve taste, not the other way around. "
                             "Profile intent is the reference frame — evaluate actual vs. what the profile intended, not pour-over or drip norms. "
                             "DE1 filter uses low pressure (1-3 bar), high flow, and long ratios (1:10-1:17) — these are all intentional, not problems. "
                             "One variable at a time — never recommend changing multiple things at once. "
                             "Track progress across brews and reference previous brews to identify trends. "
                             "If grinder info is shared, consider burr geometry (flat vs conical) in your analysis. "
                             "Focus on clarity, sweetness, and balance rather than espresso-style body and intensity.";
        } else {
            m_systemPrompt = "You are an expert espresso consultant helping a user dial in their shots on a Decent DE1 profiling machine over multiple attempts. "
                             "Key principles: Taste is king — numbers serve taste, not the other way around. "
                             "Profile intent is the reference frame — evaluate actual vs. what the profile intended, not generic espresso norms. "
                             "A Blooming Espresso at 2 bar or a turbo shot at 15 seconds are not problems — they're by design. "
                             "The DE1 controls either pressure or flow (never both); when one is the target, the other is a result of puck resistance. "
                             "One variable at a time — never recommend changing multiple things at once. "
                             "Track progress across shots and reference previous shots to identify trends. "
                             "If grinder info is shared, consider burr geometry (flat vs conical) in your analysis. "
                             "Never default to generic rules like 'grind finer', 'aim for 9 bar', or '25-30 seconds' without evidence from the data.";
        }
    }

    // Add the new shot as context
    QString contextMessage = "Here's my latest shot:\n\n" + shotSummary +
                            "\n\nPlease analyze this shot and provide recommendations, considering any previous shots we've discussed.";
    addUserMessage(contextMessage);
    sendRequest();

    emit historyChanged();
    qDebug() << "AIConversation: Added new shot context, now have" << m_messages.size() << "messages";
}

void AIConversation::saveToStorage()
{
    QSettings settings;

    // Save system prompt
    settings.setValue("ai/conversation/systemPrompt", m_systemPrompt);

    // Save messages as JSON
    QJsonDocument doc(m_messages);
    settings.setValue("ai/conversation/messages", doc.toJson(QJsonDocument::Compact));

    // Save timestamp
    settings.setValue("ai/conversation/timestamp", QDateTime::currentDateTime().toString(Qt::ISODate));

    emit savedConversationChanged();
    qDebug() << "AIConversation: Saved conversation with" << m_messages.size() << "messages";
}

void AIConversation::loadFromStorage()
{
    QSettings settings;

    // Load system prompt
    m_systemPrompt = settings.value("ai/conversation/systemPrompt").toString();

    // Load messages
    QByteArray messagesJson = settings.value("ai/conversation/messages").toByteArray();
    if (!messagesJson.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(messagesJson);
        if (doc.isArray()) {
            m_messages = doc.array();
        }
    }

    // Update last response from the last assistant message
    for (int i = m_messages.size() - 1; i >= 0; i--) {
        QJsonObject msg = m_messages[i].toObject();
        if (msg["role"].toString() == "assistant") {
            m_lastResponse = msg["content"].toString();
            break;
        }
    }

    emit historyChanged();
    qDebug() << "AIConversation: Loaded conversation with" << m_messages.size() << "messages";
}

bool AIConversation::hasSavedConversation() const
{
    QSettings settings;
    QByteArray messagesJson = settings.value("ai/conversation/messages").toByteArray();
    if (messagesJson.isEmpty()) return false;

    QJsonDocument doc = QJsonDocument::fromJson(messagesJson);
    return doc.isArray() && !doc.array().isEmpty();
}
