#pragma once

#include <QObject>
#include <QJsonArray>
#include <QJsonObject>

class AIManager;

/**
 * AIConversation - Manages a multi-turn conversation with an AI provider
 *
 * This class maintains conversation history and sends the full context
 * with each request, enabling follow-up questions and continuity.
 *
 * Usage:
 *   conversation->ask("You are an espresso expert", "Analyze this shot: ...");
 *   // Later, for follow-up:
 *   conversation->followUp("What grind size would help?");
 */
class AIConversation : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool busy READ isBusy NOTIFY busyChanged)
    Q_PROPERTY(bool hasHistory READ hasHistory NOTIFY historyChanged)
    Q_PROPERTY(bool hasSavedConversation READ hasSavedConversation NOTIFY savedConversationChanged)
    Q_PROPERTY(QString lastResponse READ lastResponse NOTIFY responseReceived)
    Q_PROPERTY(QString providerName READ providerName NOTIFY providerChanged)
    Q_PROPERTY(int messageCount READ messageCount NOTIFY historyChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorOccurred)

public:
    explicit AIConversation(AIManager* aiManager, QObject* parent = nullptr);

    bool isBusy() const { return m_busy; }
    bool hasHistory() const { return !m_messages.isEmpty(); }
    QString lastResponse() const { return m_lastResponse; }
    QString providerName() const;
    int messageCount() const { return static_cast<int>(m_messages.size()); }
    QString errorMessage() const { return m_errorMessage; }

    /**
     * Start a new conversation with system prompt and initial user message
     * Clears any existing history
     */
    Q_INVOKABLE void ask(const QString& systemPrompt, const QString& userMessage);

    /**
     * Continue the conversation with a follow-up message
     * Uses the existing system prompt and history
     */
    Q_INVOKABLE void followUp(const QString& userMessage);

    /**
     * Clear conversation history
     */
    Q_INVOKABLE void clearHistory();

    /**
     * Get full conversation as formatted text (for display)
     */
    Q_INVOKABLE QString getConversationText() const;

    /**
     * Add new shot context to existing conversation (for multi-shot dialing)
     * This appends shot data as a new user message without clearing history
     */
    Q_INVOKABLE void addShotContext(const QString& shotSummary, const QString& beverageType = "espresso");

    /**
     * Save conversation history to persistent storage
     */
    Q_INVOKABLE void saveToStorage();

    /**
     * Load conversation history from persistent storage
     */
    Q_INVOKABLE void loadFromStorage();

    /**
     * Check if there's a saved conversation
     */
    Q_INVOKABLE bool hasSavedConversation() const;

signals:
    void responseReceived(const QString& response);
    void errorOccurred(const QString& error);
    void busyChanged();
    void historyChanged();
    void providerChanged();
    void savedConversationChanged();

private slots:
    void onAnalysisComplete(const QString& response);
    void onAnalysisFailed(const QString& error);

private:
    void sendRequest();
    void addUserMessage(const QString& message);
    void addAssistantMessage(const QString& message);

    AIManager* m_aiManager;
    QString m_systemPrompt;
    QJsonArray m_messages;  // Array of {role, content} objects
    QString m_lastResponse;
    QString m_errorMessage;
    bool m_busy = false;
};
