#pragma once

#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <optional>

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
#ifdef DECENZA_TESTING
class tst_AIManager;
#endif

class AIConversation : public QObject {
    Q_OBJECT
#ifdef DECENZA_TESTING
    friend class tst_AIManager;
#endif

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
     * profileTitle/profileType/profileKbId are forwarded to shotAnalysisSystemPrompt()
     * for profile-aware knowledge injection when initializing a new conversation.
     */
    Q_INVOKABLE void addShotContext(const QString& shotSummary, const QString& shotLabel,
                                     const QString& beverageType = "espresso",
                                     const QString& profileTitle = QString(),
                                     const QString& profileType = QString(),
                                     const QString& profileKbId = QString());

    /**
     * Process a shot summary for conversation: prepends a "changes from previous" section.
     * Call this before sending via ask()/followUp() to avoid redundant data.
     * shotLabel is a human-readable date/time string (e.g. "Feb 15, 14:30") identifying the shot.
     */
    Q_INVOKABLE QString processShotForConversation(const QString& shotSummary, const QString& shotLabel);

    /**
     * Get the full system prompt for multi-shot conversations.
     * Uses the profile-aware system prompt (base + per-profile knowledge) plus multi-shot guidance.
     */
    Q_INVOKABLE QString multiShotSystemPrompt(const QString& beverageType = "espresso",
                                               const QString& profileTitle = QString(),
                                               const QString& profileType = QString(),
                                               const QString& profileKbId = QString());

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

    /**
     * Return the parsed `structuredNext` JSON object stored on the
     * assistant turn at `index`, or std::nullopt when the turn has no
     * stored structured block (clarifying-question response, legacy
     * conversation predating the field, or non-assistant role at the
     * given index). `index` is 0-based into the full m_messages array.
     */
    std::optional<QJsonObject> structuredNextForTurn(qsizetype index) const;

    /**
     * Convenience accessor: structured block on the most recent assistant
     * turn, or std::nullopt when none exists.
     */
    std::optional<QJsonObject> structuredNextForLastAssistantTurn() const;

    /**
     * Bind the resolved shot id to the current user/assistant turn pair.
     * Called by AIManager::analyzeShotWithMetadata once the resolved
     * shot is known and BEFORE the assistant message is appended; the
     * id is applied retroactively to the latest user turn AND latched
     * onto the next-appended assistant turn so a turn pair shares one
     * shotId. Calling twice for the same pair is last-write-wins.
     *
     * Issue #1053 — without this linkage, recentAdvice cannot attribute
     * a prior advisor recommendation to the shot it was about, and the
     * follow-up shot lookup has no anchor.
     */
    void setShotIdForCurrentTurn(qint64 shotId);

    /**
     * Return the shotId stored on the turn at `index`, or 0 when the
     * turn has no recorded shot (legacy conversation, free-form
     * follow-up, or out-of-bounds index).
     */
    qint64 shotIdForTurn(qsizetype index) const;

    /**
     * Compact view of one historical assistant turn for the recentAdvice
     * builder. Carries the shotId the advisor was asked about, the
     * prose (for diagnostic/preview use), and the parsed structuredNext
     * block. Only assistant turns with BOTH a non-zero shotId AND a
     * stored structuredNext are returned by recentAssistantTurns().
     */
    struct HistoricalAssistantTurn {
        qint64 shotId = 0;
        QString content;
        QJsonObject structuredNext;
    };

    /**
     * Return up to `max` qualifying assistant turns from this conversation,
     * most-recent-first. A turn qualifies iff it has BOTH a non-zero
     * shotId AND a stored structuredNext object. Turns without one or
     * the other are skipped — they do not consume a slot in the result.
     */
    QList<HistoricalAssistantTurn> recentAssistantTurns(qsizetype max) const;

    /**
     * Static helper for surfaces that want the recent-turn view but
     * don't have an instantiated AIConversation. Reads QSettings
     * directly (same `ai/conversations/<key>/messages` shape as
     * loadFromStorage) and applies the same qualifying-turn filter.
     * Used by `ai_advisor_invoke` to derive recentAdvice for the
     * resolved shot's bean+profile conversation key.
     */
    static QList<HistoricalAssistantTurn> loadRecentAssistantTurnsForKey(
        const QString& storageKey, qsizetype max);

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
    // Append an assistant message. When `structuredNext` carries a value,
    // it is persisted on the entry as a sibling of `role` and `content`
    // (see openspec/changes/add-structured-next-shot). Absent → no key
    // written; older saved conversations stay readable unchanged.
    void addAssistantMessage(const QString& message,
                             const std::optional<QJsonObject>& structuredNext = std::nullopt);
    void trimHistory();
    static QString summarizeShotMessage(const QString& content);
    static QString summarizeAdvice(const QString& response);

    // Legacy fallback: extracts the `shotAnalysis` prose from the JSON
    // envelope when present, otherwise returns the message unchanged.
    // Used only by `extractShotFields` for the legacy-prose detector
    // substring checks. New code should prefer `extractShotFields`.
    static QString extractShotProse(const QString& content);

    // Structured per-shot data extracted from a user message — issue
    // #1039. Numeric fields are kept as `QString` because the consumers
    // render them into prose diffs ("Dose 18.0g→20.0g") and need to
    // preserve the original precision. Empty string means "field
    // absent" — the diff/summary code skips fields that are absent on
    // either side, mirroring the legacy regex semantics.
    struct ShotFields {
        QString shotLabel;          // from "## Shot (label)" outer header
        QString doseG;
        QString yieldG;
        QString durationSec;
        QString grinder;            // pre-formatted "<brand> <model> (<burrs>) at <setting>"
        QString profileTitle;
        QString score;
        QString notes;
        bool channelingDetected = false;
        bool temperatureUnstable = false;
        bool fromStructuredEnvelope = false;  // false ⇒ legacy regex path fired
    };

    // Read structured per-shot fields out of a user message. Prefers
    // the JSON envelope's `shot` / `currentBean` / `profile` blocks;
    // falls back to legacy regex on the prose body when JSON parsing
    // fails. Pure function.
    static ShotFields extractShotFields(const QString& content);

    struct PreviousShotInfo { QString content; QString shotLabel; };
    PreviousShotInfo findPreviousShot(const QString& excludeLabel = QString()) const;

    static constexpr int MAX_VERBATIM_PAIRS = 2;

    // Outer-wrapper regex for the "## Shot (date)" header that
    // `addShotContext` prepends OUTSIDE the JSON envelope.
    static const QRegularExpression s_shotLabelRe;

    // Legacy fallback regexes. Used only by `extractShotFields` when
    // the JSON envelope cannot be parsed (stored conversations from
    // before issue #1034 / #1039). Do not add new callers.
    static const QRegularExpression s_doseRe, s_yieldRe, s_durationRe,
        s_grinderRe, s_profileRe, s_scoreRe, s_notesRe;

    AIManager* m_aiManager;
    QString m_systemPrompt;
    // Array of {role, content[, shotId?, structuredNext?]} objects.
    // shotId is the resolved shot id the advisor was asked about for
    // the turn pair (issue #1053); structuredNext is present only on
    // assistant turns whose response ended with a parseable nextShot
    // recommendation block (issue #1054). Both fields are absent on
    // legacy conversations saved before their respective changes — the
    // readers return 0 / nullopt without erroring.
    QJsonArray m_messages;
    // Latch for setShotIdForCurrentTurn: when non-zero, the next
    // addAssistantMessage call stamps the same shotId onto the new
    // assistant entry so the user/assistant pair shares it. Reset after
    // application so a subsequent followUp without a setShotIdForCurrentTurn
    // produces an unstamped turn (free-form follow-up).
    qint64 m_pendingShotId = 0;
    QString m_lastResponse;
    QString m_errorMessage;
    bool m_busy = false;
    QString m_storageKey;     // Current conversation's storage slot key
    QString m_contextLabel;   // Display label e.g. "Ethiopian Sidamo / D-Flow"
};
