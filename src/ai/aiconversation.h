#pragma once

#include <QObject>
#include <QJsonArray>
#include <QHash>
#include <QSet>
#include <QJsonObject>
#include <QRegularExpression>
#include <optional>
#include <QtQml/qqmlregistration.h>

class AIManager;
class TranslationManager;
class AppSettings;

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

    // Reached from QML only as an AIManager property, never constructed there. Registered at
    // COMPILE time so qmllint, qmlcachegen and the language server can follow the property
    // through to this class; a runtime qmlRegister* call is invisible to all three.
    QML_ELEMENT
    QML_UNCREATABLE("AIConversation is created in C++ and reached via AIManager")
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
    Q_PROPERTY(bool canRetry READ canRetry NOTIFY canRetryChanged)
    Q_PROPERTY(QString contextLabel READ contextLabel NOTIFY contextLabelChanged)

public:
    explicit AIConversation(AIManager* aiManager, QObject* parent = nullptr);

    // Inject the TranslationManager so user-visible error strings localize.
    // Set by AIManager::setTranslationManager; until injected, tr_() returns
    // the English fallback.
    void setTranslationManager(TranslationManager* tm) { m_translationManager = tm; }

    bool isBusy() const { return m_busy; }
    bool hasHistory() const { return !m_messages.isEmpty(); }
    QString lastResponse() const { return m_lastResponse; }
    QString providerName() const;
    int messageCount() const { return static_cast<int>(m_messages.size()); }
    QString errorMessage() const { return m_errorMessage; }
    // True when the last turn is an unanswered user message and no request is
    // in flight — i.e. the previous request failed and can be re-sent verbatim.
    bool canRetry() const;
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
     * Re-send the last failed turn. Valid only when canRetry() is true (not
     * busy, last turn is an unanswered user message). Re-uses the stored
     * message and system prompt verbatim — does not append a new turn and does
     * not re-run the per-follow-up rating/metadata-capture hooks.
     */
    Q_INVOKABLE void retry();

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
     * Drop turn `shotId`s that no longer name a shot in this device's database.
     *
     * Called by loadFromStorage, and again by AIManager::setShotHistoryStorage
     * for the conversation the manager loaded in its own constructor — before
     * any storage was wired. Without that second call the most recently used
     * conversation, the one the user is most likely to continue, loads
     * unrepaired on every launch.
     *
     * A no-op when no storage is wired yet, and when the database cannot answer
     * (see ShotHistoryStorage::existingShotIds). Safe to call repeatedly.
     */
    void repairStaleTurnShotIds();

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
     * Call once the resolved shot for the current turn is known and
     * BEFORE the assistant message is appended; the
     * id is applied retroactively to the latest user turn AND latched
     * onto the next-appended assistant turn so a turn pair shares one
     * shotId. Calling twice for the same pair is last-write-wins.
     *
     * Issue #1053 — without this linkage, recentAdvice cannot attribute
     * a prior advisor recommendation to the shot it was about, and the
     * follow-up shot lookup has no anchor.
     */
    Q_INVOKABLE void setShotIdForCurrentTurn(qint64 shotId);

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
     *
     * Reading QSettings directly means it does NOT go through
     * repairStaleTurnShotIds — on an install carrying pre-remap
     * conversations the ids here can still name the source database.
     *
     * That is bounded, not fixed: DialingBlocks::buildRecentAdviceBlock, the
     * only consumer, drops a turn whose shot row is missing and drops one whose
     * profile_kb_id does not match the shot under analysis. What survives is a
     * stale id that happens to hit an existing shot on the SAME profile. There
     * is no id to correct it to from here, and duplicating the existence check
     * the consumer already performs would not narrow it further.
     */
    static QList<HistoricalAssistantTurn> loadRecentAssistantTurnsForKey(
        const QString& storageKey, qsizetype max);

    /**
     * Persist a user/assistant turn pair into the conversation at
     * `storageKey` without going through a live AIConversation. Used by
     * `ai_advisor_invoke` (MCP) so its turns participate in
     * `recentAdvice` attribution on subsequent calls — the in-app
     * advisor writes via `ask()` + `addAssistantMessage`; this helper
     * gives the MCP path the same write-through without needing to
     * mutate the user's currently-active in-app conversation object.
     *
     * Layout: same `ai/conversations/<key>/messages` shape as
     * `saveToStorage`. The user turn carries `shotId`; the assistant
     * turn carries `shotId` + optional `structuredNext`.
     *
     * Concurrency: when the live in-app `AIConversation` has the same
     * `storageKey()` loaded, the caller is responsible for refreshing
     * its in-memory state via `loadFromStorage()` after this returns —
     * otherwise the in-app's next `saveToStorage` will overwrite the
     * just-written turn.
     */
    /**
     * `systemPrompt` is the prompt a later IN-APP turn should run under. It is
     * stored only when the key has none yet, so appending to a thread the in-app
     * advisor started leaves that thread's own prompt alone.
     *
     * Not defaulted, deliberately. A stored thread with no system prompt loads
     * with history but an empty `m_systemPrompt`, and `followUp()` refuses it —
     * "Please start a new conversation first" — so the user's only way forward
     * in the app is Clear, which DELETES the turns written here. Pass an empty
     * string only when that is the intended outcome (an MCP caller supplying its
     * own one-off `systemPromptOverride`, which must not become the thread's
     * durable prompt). Doing so is logged as a warning when the key has no
     * prompt of its own — which is the case that strands the thread; appending
     * an empty prompt to a key that already has one is silent and harmless.
     *
     * Note this is not necessarily the prompt the turn was GENERATED under —
     * see the persist-vs-run distinction at the mcptools_ai.cpp call site.
     */
    static void appendAssistantTurnForKey(
        const QString& storageKey,
        qint64 shotId,
        const QString& userPrompt,
        const QString& assistantResponse,
        const std::optional<QJsonObject>& structuredNext,
        const QString& systemPrompt);

    /**
     * What importConversationsStatic did. Note the units differ: the first
     * field counts CONVERSATIONS, the last two count TURNS.
     *
     * Skipped-as-duplicate and malformed entries are counted inside the
     * importer and go to its own log line, not here: a field only its producer
     * reads is weight on a shared type, and no caller has anywhere to show
     * them.
     */
    struct ImportTally {
        int conversationsImported = 0;  // written to storage
        int turnsRemapped = 0;          // turn shotIds rewritten to a destination id
        int turnsCleared = 0;           // turn shotIds dropped, source shot not in the map
    };

    /**
     * Import conversations from a backup or a peer device into QSettings.
     *
     * ONE definition. This loop was hand-written THREE times — in
     * DatabaseBackupManager (restore), DataMigrationClient (LAN migration)
     * and the /api/backup/restore endpoint — and the copies were identical
     * only by luck. Adding the shotId remap below to one and not the others
     * was the obvious next failure, which is why they were collapsed before
     * it was added.
     *
     * `shotIdMap` maps SOURCE shot ids to the ids those shots received in
     * THIS device's database, as produced by
     * ShotHistoryStorage::importDatabaseStatic. Each turn's `shotId` is
     * rewritten through it. A source id the map does not contain has its
     * `shotId` REMOVED, leaving the turn in the documented null state (no
     * key) rather than holding an id that names a foreign database.
     *
     * Passing nullptr means "no shots came with these conversations" — the
     * conversations-only import path — and clears every `shotId`. That is
     * the same rule, not an exception to it: an absent map is the degenerate
     * case of an absent entry. Keeping the ids there would leave every turn
     * pointing into a database this device does not have.
     *
     * DO NOT call this at all when the shot import was REFUSED
     * (ShotHistoryStorage::ImportResult::refused()). A refusal is provoked by a transient
     * condition — a mid-scan SQLITE_BUSY — so the user's natural response is
     * to run the restore again. Importing the conversations now with every id
     * cleared makes that retry useless: the keys are already present, the
     * retry skips them as duplicates, and the linkage is gone permanently.
     * Import nothing and let the retry do it properly.
     *
     * Callers keep their own policy: replace-mode pre-clearing, sync(), and
     * reloading the live conversation all stay with the caller.
     *
     * @param settings   open settings object to write through
     * @param conversations  the incoming array, as carried by the backup
     *                       archive or the migration endpoint
     * @param shotIdMap  source->destination shot ids, or nullptr
     */
    static ImportTally importConversationsStatic(
        AppSettings& settings,
        const QJsonArray& conversations,
        const QHash<qint64, qint64>* shotIdMap);

    /**
     * Drop `shotId` from every turn that names a shot the database does not
     * have, so a stale id is never read back as a live one.
     *
     * Repairs conversations imported before the remap above existed: those
     * hold ids from the source database, and shot ids only ever increase, so
     * an untouched stale id eventually becomes a VALID id belonging to an
     * unrelated shot. Returns how many it cleared.
     *
     * This mutates the array in place and the drop DOES become permanent:
     * the caller's next saveToStorage() writes the array verbatim. That is
     * acceptable only because the id is known not to resolve. Never call this
     * with a set that might merely be UNANSWERED — see
     * ShotHistoryStorage::existingShotIds, which returns nullopt rather than
     * an empty set for exactly that reason.
     */
    static int dropUnresolvableShotIds(QJsonArray& messages,
                                       const QSet<qint64>& existingShotIds);

signals:
    void responseReceived(const QString& response);
    void errorOccurred(const QString& error);
    void busyChanged();
    void historyChanged();
    void canRetryChanged();
    void contextLabelChanged();
    void providerChanged();
    void savedConversationChanged();

    // A thread was just WRITTEN to disk under this object's storage key.
    // Distinct from savedConversationChanged(), which is the NOTIFY for
    // hasSavedConversation and therefore also fires on load and on clear —
    // states in which nothing was written and the live identity may not yet
    // match the key. Only saveToStorage() emits this one.
    void conversationPersisted();

private slots:
    void onAnalysisComplete(const QString& response);
    void onAnalysisFailed(const QString& error);

private:
    void sendRequest();
    // Translate a user-visible string via the injected TranslationManager,
    // falling back to the English source when none is set.
    QString tr_(const char* key, const char* fallback) const;
    // Drop a trailing unanswered user turn (a turn kept by onAnalysisFailed for
    // retry) before appending a new user message, so we never send two
    // consecutive user-role messages. No-op unless the last entry is a user turn.
    void dropTrailingFailedUserTurn();
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
    static QString stripStructuredNextBlock(const QString& content);

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
    TranslationManager* m_translationManager = nullptr;
    QString m_systemPrompt;
    // Array of {role, content[, shotId?, structuredNext?]} objects.
    // shotId is the resolved shot id the advisor was asked about for
    // the turn pair (issue #1053); structuredNext is present only on
    // assistant turns whose response ended with a parseable nextShot
    // recommendation block (issue #1054). Both fields are absent on
    // legacy conversations saved before their respective changes — the
    // readers return 0 / nullopt without erroring.
    QJsonArray m_messages;
    // Raw messages appended via addUserMessage/addAssistantMessage since the
    // last successful loadFromStorage()/saveToStorage() — i.e. exactly what
    // this object added that isn't confirmed to be on disk yet. Deliberately
    // tracked separately from m_messages (not as a size/count) because
    // trimHistory() rewrites m_messages — compacting older verbatim turns
    // into a synthetic summary — which would desync a simple "messages
    // synced so far" counter from m_messages.size() and silently break the
    // splice below. saveToStorage() compares this against the current disk
    // contents to detect when another writer (AIConversation::
    // appendAssistantTurnForKey, used by the MCP ai_advisor_invoke path) has
    // appended turns to the same storage key since we last synced, so it can
    // splice these onto the current disk contents instead of blindly
    // overwriting them away. Cleared by loadFromStorage() (freshly synced)
    // and by ask()/clearHistory()/resetInMemory() (starting over — nothing
    // pending to splice).
    QJsonArray m_unsyncedMessages;

    // Turn shotIds repairStaleTurnShotIds() dropped this session. Kept so
    // saveToStorage can strip them again after reconciling against another
    // writer's on-disk copy, which still carries them.
    QSet<qint64> m_forgottenShotIds;
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
