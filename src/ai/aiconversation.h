#pragma once

#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>

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
    Q_PROPERTY(QString contextLabel READ contextLabel NOTIFY contextLabelChanged)

public:
    explicit AIConversation(AIManager* aiManager, QObject* parent = nullptr);

    bool isBusy() const { return m_busy; }
    bool hasHistory() const { return !m_messages.isEmpty(); }
    QString lastResponse() const { return m_lastResponse; }
    QString providerName() const;
    int messageCount() const { return static_cast<int>(m_messages.size()); }
    QString errorMessage() const { return m_errorMessage; }
    QString contextLabel() const { return m_contextLabel; }

    QString storageKey() const { return m_storageKey; }
    void setStorageKey(const QString& key);
    void setContextLabel(const QString& brand, const QString& type, const QString& profile);

    /**
     * Start a new conversation with system prompt and initial user message
     * Clears any existing history
     */
    Q_INVOKABLE void ask(const QString& systemPrompt, const QString& userMessage);

    /**
     * Continue the conversation with a follow-up message
     * Uses the existing system prompt and history
     */
    Q_INVOKABLE bool followUp(const QString& userMessage);

    /**
     * Clear conversation history
     */
    Q_INVOKABLE void clearHistory();

    /**
     * Clear in-memory state without touching QSettings.
     * Used by switchConversation() to reset before loading a different conversation.
     */
    void resetInMemory();

    /**
     * Get full conversation as formatted text (for display)
     */
    Q_INVOKABLE QString getConversationText() const;

    /**
     * Get the system prompt used for this conversation (for AI report)
     */
    Q_INVOKABLE QString getSystemPrompt() const { return m_systemPrompt; }

    /**
     * Add new shot context to existing conversation (for multi-shot dialing)
     * This appends shot data as a new user message without clearing history.
     * shotLabel is a human-readable date/time string (e.g. "Feb 15, 14:30") identifying the shot.
     */
    Q_INVOKABLE void addShotContext(const QString& shotSummary, const QString& shotLabel, const QString& beverageType = "espresso");

    /**
     * Process a shot summary for conversation: prepends a "changes from previous" section.
     * Call this before sending via ask()/followUp() to avoid redundant data.
     * shotLabel is a human-readable date/time string (e.g. "Feb 15, 14:30") identifying the shot.
     */
    Q_INVOKABLE QString processShotForConversation(const QString& shotSummary, const QString& shotLabel);

    /**
     * Get the full system prompt for multi-shot conversations.
     * Uses the rich single-shot system prompt plus multi-shot guidance.
     */
    Q_INVOKABLE QString multiShotSystemPrompt(const QString& beverageType = "espresso");

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
    void contextLabelChanged();
    void providerChanged();
    void savedConversationChanged();

private slots:
    void onAnalysisComplete(const QString& response);
    void onAnalysisFailed(const QString& error);

private:
    void sendRequest();
    void addUserMessage(const QString& message);
    void addAssistantMessage(const QString& message);
    void trimHistory();
    static QString summarizeShotMessage(const QString& content);
    static QString summarizeAdvice(const QString& response);
    static QString extractMetric(const QString& content, const QRegularExpression& re);

    struct PreviousShotInfo { QString content; QString shotLabel; };
    PreviousShotInfo findPreviousShot(const QString& excludeLabel = QString()) const;

    static constexpr int MAX_VERBATIM_PAIRS = 2;

    // Shared regex constants for shot message parsing
    static const QRegularExpression s_doseRe, s_yieldRe, s_durationRe,
        s_grinderRe, s_profileRe, s_shotLabelRe, s_scoreRe, s_notesRe;

    AIManager* m_aiManager;
    QString m_systemPrompt;
    QJsonArray m_messages;  // Array of {role, content} objects
    QString m_lastResponse;
    QString m_errorMessage;
    bool m_busy = false;
    QString m_storageKey;     // Current conversation's storage slot key
    QString m_contextLabel;   // Display label e.g. "Ethiopian Sidamo / D-Flow"
};
