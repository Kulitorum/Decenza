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
    // Built by AIManager::ConversationEntry::label() — the single producer.
    void setContextLabel(const QString& label);

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

    // What changed between this shot and the previous shot in this conversation,
    // as a field on the turn's payload rather than a prose banner glued to the
    // front of it. Empty object when there is no previous shot to compare with;
    // `anyChange: false` when there is one and nothing moved — which the model
    // needs as much as a diff, since it separates "your change did nothing" from
    // "you changed nothing".
    QJsonObject changesFromPreviousShot(const QString& shotLabel,
                                        const QString& shotPayload) const;

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
    static void appendAssistantTurnForKey(
        const QString& storageKey,
        qint64 shotId,
        const QString& userPrompt,
        const QString& assistantResponse,
        const std::optional<QJsonObject>& structuredNext);

    /**
     * What importConversationsStatic did. Note the units differ: the first
     * three fields count CONVERSATIONS, the last two count TURNS.
     *
     * Skipped-as-duplicate and malformed entries are counted inside the
     * importer and go to its own log line, not here: the user's copy is already
     * the live one, and a damaged archive entry names no remedy.
     *
     * The two REFUSED counts are different — each names something the user can
     * act on, and reporting only `conversationsImported` turns a run that
     * dropped 37 of 40 into a green "3 imported". They are separate fields
     * because the remedy differs, which is the whole reason to show them.
     */
    struct ImportTally {
        int conversationsImported = 0;  // written to storage
        // Named a package this device could not identify, because no equipment
        // accompanied them. Remedy: import the shots too — the shot import is
        // what carries equipment across and produces the id map.
        int refusedNeedShots = 0;
        // Keyed before the equipment package joined the key. No shot on this
        // device derives that key, so the thread could never be opened again.
        // No remedy; stated so the count is not mistaken for the one above.
        int refusedLegacyKey = 0;
        int turnsRemapped = 0;          // turn shotIds rewritten to a destination id
        int turnsCleared = 0;           // turn shotIds dropped, source shot not in the map

        int refused() const { return refusedNeedShots + refusedLegacyKey; }
    };

    /**
     * One user-facing sentence for what an import refused, or empty when it
     * refused nothing.
     *
     * ONE producer for the same reason the importer is one: three surfaces
     * report this run (the in-app import popup, the device-migration dialog and
     * the ShotServer restore page), and a count without its remedy is what made
     * the silence worth fixing in the first place. Already specific about the
     * fix; callers pair it with their own lead-in.
     *
     * `tm` may be null, which yields the English fallback — the same contract as
     * the tr_() helper every other user-visible string in this file uses. It is a
     * parameter because this is static and tr_() is not. NOT QObject::tr(): this
     * app installs no QTranslator at all, so QObject::tr() would make the string
     * permanently English AND render "%n" literally, giving "1 conversation(s)".
     *
     * Main thread only, like everything reaching TranslationManager.
     */
    static QString importRefusalNote(const ImportTally& tally, TranslationManager* tm);

    /**
     * The note for conversations HELD BACK — not refused inside the importer,
     * but never handed to it, because the shot import was refused and importing
     * now would consume their keys and make the retry useless.
     *
     * Separate from importRefusalNote because there is no ImportTally to report:
     * the importer did not run. Same null-`tm` contract, same main-thread rule.
     * Empty when `count` is 0.
     */
    static QString importHeldBackNote(qsizetype count, TranslationManager* tm);

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
     * `equipmentIdMap` is the same shape for equipment packages, from
     * ShotHistoryStorage::ImportResult::equipmentIdMap. It is needed because the
     * conversation KEY is derived from the package id: the incoming key names a
     * package row in the SOURCE database, and importing under it produces a
     * thread no shot on this device can open — or worse, one that collides with
     * a destination package that happens to share the source's row id.
     *
     * So each conversation is RE-KEYED through the map before it is written,
     * and two shapes are refused rather than imported wrong:
     *   - a conversation whose carried key is not what these fields derive
     *     (an archive written before the key included the package), because
     *     there is no shot on this device that would ever open it;
     *   - a conversation naming a package the map does not contain, because
     *     bucket 0 would put a thread about one basket into the unpackaged
     *     pool, which is the contamination the key exists to prevent.
     * Both are counted on ImportTally (`refusedNeedShots` / `refusedLegacyKey`)
     * and rendered by `importRefusalNote`. They were briefly log-only, on the
     * reasoning that no caller had anywhere to show them — which was wrong:
     * every caller already reports `conversationsImported` to the user, so the
     * survivors were shown and the casualties were not.
     *
     *
     * @param settings   open settings object to write through
     * @param conversations  the incoming array, as carried by the backup
     *                       archive or the migration endpoint
     * @param shotIdMap  source->destination shot ids, or nullptr
     * @param equipmentIdMap  source->destination equipment package ids, or
     *                        nullptr when no equipment accompanied them
     */
    static ImportTally importConversationsStatic(
        AppSettings& settings,
        const QJsonArray& conversations,
        const QHash<qint64, qint64>* shotIdMap,
        const QHash<qint64, qint64>* equipmentIdMap);

    /**
     * Serialize every indexed conversation into the array the importer above
     * reads. ONE producer, for the same reason as the importer: this loop was
     * hand-written twice — in DatabaseBackupManager (archive) and ShotServer
     * (`/api/backup/ai-conversations`) — and the two copies had to agree on
     * every field name for a restore to reassemble what a backup wrote. The
     * equipment fields are exactly the kind of addition that lands in one copy
     * and not the other, and the failure is silent: the archive is written, the
     * restore succeeds, and the conversation is keyed to nothing.
     *
     * Walks `ai/conversations/index`, so a conversation with no index entry is
     * not exported — matching what both copies already did.
     */
    static QJsonArray exportConversationsStatic(AppSettings& settings);

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

    // Structured per-shot data extracted from a user message. Numeric fields
    // are kept as `QString` so the change-detection output preserves the
    // precision the payload carried. Empty string means "field absent" — the
    // diff skips fields absent on either side.
    struct ShotFields {
        QString shotLabel;          // the payload's own `shotLabel` field
        QString doseG;
        QString yieldG;
        QString durationSec;
        QString grinder;            // pre-formatted "<brand> <model> (<burrs>) at <setting>"
        QString profileTitle;
        QString score;
        QString notes;
        bool channelingDetected = false;
    };

    // Read structured per-shot fields out of a user message. The message IS a
    // JSON object; fields come from its `shot` / `currentBean` / `profile`
    // blocks. Pure function.
    static ShotFields extractShotFields(const QString& content);

    struct PreviousShotInfo { QString content; QString shotLabel; };
    PreviousShotInfo findPreviousShot(const QString& excludeLabel = QString()) const;

    static constexpr int MAX_VERBATIM_PAIRS = 2;

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
