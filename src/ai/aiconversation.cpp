#include "aiconversation.h"
#include "core/appsettings.h"
#include "aimanager.h"
#include "conversationkey.h"
#include "shotsummarizer.h"
#include "../core/translationmanager.h"
// Full type needed: loadFromStorage calls existingShotIds() to forget turn
// shot references that no longer resolve.
#include "../history/shothistorystorage.h"

#include <QDateTime>
#include <QDebug>
#include <QSettings>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QRegularExpression>

QString AIConversation::tr_(const char* key, const char* fallback) const
{
    if (m_translationManager)
        return m_translationManager->translateString(QString::fromUtf8(key),
                                               QString::fromUtf8(fallback));
    return QString::fromUtf8(fallback);
}

AIConversation::AIConversation(AIManager* aiManager, QObject* parent)
    : QObject(parent)
    , m_aiManager(aiManager)
{
    // Connect to AIManager conversation-specific signals (not shared analyze signals)
    if (m_aiManager) {
        connect(m_aiManager, &AIManager::conversationResponseReceived,
                this, &AIConversation::onAnalysisComplete);
        connect(m_aiManager, &AIManager::conversationErrorOccurred,
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
    if (!m_aiManager) {
        qWarning() << "AIConversation::ask called without AIManager";
        m_errorMessage = tr_("ai.error.notAvailable", "AI not available");
        emit errorOccurred(m_errorMessage);
        return;
    }
    if (m_busy) {
        qDebug() << "AIConversation::ask ignored — already busy";
        return;
    }

    // Clear previous conversation and start fresh
    m_messages = QJsonArray();
    m_unsyncedMessages = QJsonArray();
    m_systemPrompt = systemPrompt;
    m_lastResponse.clear();
    m_errorMessage.clear();

    addUserMessage(userMessage);
    sendRequest();

    emit historyChanged();
}

bool AIConversation::followUp(const QString& userMessage)
{
    if (!m_aiManager) {
        qWarning() << "AIConversation::followUp called without AIManager";
        m_errorMessage = tr_("ai.error.notAvailable", "AI not available");
        emit errorOccurred(m_errorMessage);
        return false;
    }
    if (m_busy) {
        qDebug() << "AIConversation::followUp ignored — already busy";
        return false;
    }
    if (m_systemPrompt.isEmpty()) {
        qWarning() << "AIConversation::followUp called without prior ask()";
        m_errorMessage = tr_("ai.error.startNewFirst", "Please start a new conversation first");
        emit errorOccurred(m_errorMessage);
        return false;
    }

    m_errorMessage.clear();

    // The user is sending a NEW message instead of retrying a failed turn, so
    // drop any stale unanswered turn first (see dropTrailingFailedUserTurn).
    // Runs before the rating hook below, which scans for the prior *assistant*
    // turn and is therefore unaffected by removing a trailing user turn.
    dropTrailingFailedUserTurn();

    // Closed-loop rating capture (issue #1055 Layer 1). When the prior
    // assistant turn asked the user about taste AND the reply carries
    // a numeric score, persist that score back to ShotProjection so
    // bestRecentShot starves less. Runs BEFORE addUserMessage because
    // we need the *prior* assistant message — and BEFORE sendRequest
    // because we don't want the rating to depend on the network round
    // trip succeeding.
    QString priorAssistant;
    qint64 turnShotId = 0;
    for (qsizetype i = m_messages.size() - 1; i >= 0; --i) {
        const QJsonObject msg = m_messages.at(i).toObject();
        if (msg.value("role").toString() == QStringLiteral("assistant")) {
            priorAssistant = msg.value("content").toString();
            turnShotId = static_cast<qint64>(msg.value("shotId").toDouble());
            break;
        }
    }
    if (!priorAssistant.isEmpty() && turnShotId > 0) {
        m_aiManager->maybePersistRatingFromReply(userMessage, priorAssistant, turnShotId);
        // shot-metadata-capture: same anchored-turn invariant as the rating
        // hook above. Both can fire on the same reply (e.g. "82, dark roast,
        // balanced" → enjoyment=82 AND roastLevel="Dark").
        m_aiManager->maybePersistBeanCorrectionFromReply(userMessage, priorAssistant, turnShotId);
    }

    addUserMessage(userMessage);
    sendRequest();

    emit historyChanged();
    return true;
}

void AIConversation::clearHistory()
{
    // Clear stored data for current key
    if (!m_storageKey.isEmpty()) {
        AppSettings settings;
        QString prefix = "ai/conversations/" + m_storageKey + "/";
        settings.remove(prefix + "systemPrompt");
        settings.remove(prefix + "messages");
        settings.remove(prefix + "timestamp");
    }

    m_messages = QJsonArray();
    m_unsyncedMessages = QJsonArray();
    m_systemPrompt.clear();
    m_lastResponse.clear();
    m_errorMessage.clear();

    emit historyChanged();
    emit canRetryChanged();
    emit savedConversationChanged();
    qDebug() << "AIConversation: History cleared for key:" << m_storageKey;
}

void AIConversation::resetInMemory()
{
    m_messages = QJsonArray();
    m_unsyncedMessages = QJsonArray();
    m_systemPrompt.clear();
    m_lastResponse.clear();
    m_errorMessage.clear();
    emit historyChanged();
    emit canRetryChanged();
}

void AIConversation::setStorageKey(const QString& key)
{
    m_storageKey = key;
}

void AIConversation::setContextLabel(const QString& label)
{
    m_contextLabel = label;
    emit contextLabelChanged();
}

void AIConversation::addUserMessage(const QString& message)
{
    QJsonObject msg;
    msg["role"] = "user";
    msg["content"] = message;
    // Apply the latched shotId to the new user turn (issue #1053) but
    // do NOT consume the latch — the assistant message that follows
    // shares the pair's shotId. Production flow is
    // setShotIdForCurrentTurn → ask() → addUserMessage here →
    // addAssistantMessage (which clears the latch).
    if (m_pendingShotId != 0) {
        msg["shotId"] = static_cast<double>(m_pendingShotId);
    }
    m_messages.append(msg);
    m_unsyncedMessages.append(msg);
}

void AIConversation::addAssistantMessage(const QString& message,
                                          const std::optional<QJsonObject>& structuredNext)
{
    QJsonObject msg;
    msg["role"] = "assistant";
    msg["content"] = message;
    if (structuredNext.has_value()) {
        msg["structuredNext"] = *structuredNext;
    }
    if (m_pendingShotId != 0) {
        msg["shotId"] = static_cast<double>(m_pendingShotId);
        m_pendingShotId = 0;  // consume the latch
    }
    m_messages.append(msg);
    m_unsyncedMessages.append(msg);
}

std::optional<QJsonObject> AIConversation::structuredNextForTurn(qsizetype index) const
{
    if (index < 0 || index >= m_messages.size()) return std::nullopt;
    const QJsonObject msg = m_messages.at(index).toObject();
    if (msg.value("role").toString() != QStringLiteral("assistant")) return std::nullopt;
    if (!msg.contains(QStringLiteral("structuredNext"))) return std::nullopt;
    const QJsonValue v = msg.value(QStringLiteral("structuredNext"));
    if (!v.isObject()) return std::nullopt;
    return v.toObject();
}

std::optional<QJsonObject> AIConversation::structuredNextForLastAssistantTurn() const
{
    for (qsizetype i = m_messages.size() - 1; i >= 0; --i) {
        const QJsonObject msg = m_messages.at(i).toObject();
        if (msg.value("role").toString() == QStringLiteral("assistant")) {
            return structuredNextForTurn(i);
        }
    }
    return std::nullopt;
}

void AIConversation::setShotIdForCurrentTurn(qint64 shotId)
{
    m_pendingShotId = shotId;
    // Retroactively stamp the most recent user turn ONLY when it
    // doesn't already carry a shotId — protects the prior pair's
    // attribution when this is called between turns of an accumulating
    // conversation. The latch above covers the future-pair case
    // (the next addUserMessage / addAssistantMessage stamp from it).
    if (shotId == 0) return;
    for (qsizetype i = m_messages.size() - 1; i >= 0; --i) {
        QJsonObject msg = m_messages.at(i).toObject();
        if (msg.value("role").toString() == QStringLiteral("user")) {
            if (msg.contains(QStringLiteral("shotId"))) return;  // already attributed; don't overwrite
            msg["shotId"] = static_cast<double>(shotId);
            m_messages.replace(i, msg);
            return;
        }
    }
}

qint64 AIConversation::shotIdForTurn(qsizetype index) const
{
    if (index < 0 || index >= m_messages.size()) return 0;
    const QJsonObject msg = m_messages.at(index).toObject();
    if (!msg.contains(QStringLiteral("shotId"))) return 0;
    return static_cast<qint64>(msg.value(QStringLiteral("shotId")).toDouble());
}

QList<AIConversation::HistoricalAssistantTurn>
AIConversation::recentAssistantTurns(qsizetype max) const
{
    QList<HistoricalAssistantTurn> out;
    if (max <= 0) return out;
    for (qsizetype i = m_messages.size() - 1; i >= 0 && out.size() < max; --i) {
        const QJsonObject msg = m_messages.at(i).toObject();
        if (msg.value("role").toString() != QStringLiteral("assistant")) continue;
        const qint64 sid = static_cast<qint64>(msg.value("shotId").toDouble());
        if (sid == 0) continue;  // legacy or free-form turn
        if (!msg.contains(QStringLiteral("structuredNext"))) continue;
        const QJsonValue snVal = msg.value(QStringLiteral("structuredNext"));
        if (!snVal.isObject()) continue;
        out.append(HistoricalAssistantTurn{
            sid, msg.value("content").toString(), snVal.toObject()
        });
    }
    return out;
}

QList<AIConversation::HistoricalAssistantTurn>
AIConversation::loadRecentAssistantTurnsForKey(const QString& storageKey, qsizetype max)
{
    QList<HistoricalAssistantTurn> out;
    if (storageKey.isEmpty() || max <= 0) return out;
    AppSettings settings;
    const QString prefix = QStringLiteral("ai/conversations/") + storageKey + QStringLiteral("/");
    const QByteArray raw = settings.value(prefix + "messages").toByteArray();
    if (raw.isEmpty()) return out;
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray()) return out;
    const QJsonArray arr = doc.array();
    for (qsizetype i = arr.size() - 1; i >= 0 && out.size() < max; --i) {
        const QJsonObject msg = arr.at(i).toObject();
        if (msg.value("role").toString() != QStringLiteral("assistant")) continue;
        const qint64 sid = static_cast<qint64>(msg.value("shotId").toDouble());
        if (sid == 0) continue;
        if (!msg.contains(QStringLiteral("structuredNext"))) continue;
        const QJsonValue snVal = msg.value(QStringLiteral("structuredNext"));
        if (!snVal.isObject()) continue;
        out.append(HistoricalAssistantTurn{
            sid, msg.value("content").toString(), snVal.toObject()
        });
    }
    return out;
}

void AIConversation::appendAssistantTurnForKey(
    const QString& storageKey,
    qint64 shotId,
    const QString& userPrompt,
    const QString& assistantResponse,
    const std::optional<QJsonObject>& structuredNext)
{
    if (storageKey.isEmpty()) return;
    AppSettings settings;
    const QString prefix = QStringLiteral("ai/conversations/") + storageKey + QStringLiteral("/");

    // Pull the existing messages array (if any) so we append rather than
    // overwrite. New conversations produce an empty array.
    QJsonArray messages;
    const QByteArray raw = settings.value(prefix + "messages").toByteArray();
    if (!raw.isEmpty()) {
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
        if (err.error == QJsonParseError::NoError && doc.isArray()) {
            messages = doc.array();
        } else {
            qWarning() << "AIConversation::appendAssistantTurnForKey: existing messages "
                          "for key" << storageKey << "did not parse as JSON array — "
                          "appending to empty;" << err.errorString();
        }
    }

    QJsonObject userMsg;
    userMsg["role"] = QStringLiteral("user");
    userMsg["content"] = userPrompt;
    if (shotId != 0) userMsg["shotId"] = static_cast<double>(shotId);
    messages.append(userMsg);

    QJsonObject assistantMsg;
    assistantMsg["role"] = QStringLiteral("assistant");
    assistantMsg["content"] = assistantResponse;
    if (shotId != 0) assistantMsg["shotId"] = static_cast<double>(shotId);
    if (structuredNext.has_value()) assistantMsg["structuredNext"] = *structuredNext;
    messages.append(assistantMsg);

    settings.setValue(prefix + "messages",
        QJsonDocument(messages).toJson(QJsonDocument::Compact));
    settings.setValue(prefix + "timestamp",
        QDateTime::currentDateTime().toString(Qt::ISODate));
    // Note: systemPrompt is not written here. The in-app advisor sets it
    // via ask(); the MCP path uses analyze(systemPrompt, userPrompt) and
    // doesn't carry an AIConversation. For recentAdvice purposes the
    // system prompt isn't needed — only `messages` is read.
}

// Rewrite one conversation's turn shotIds through the import's id map.
// Split out so both the map and the no-map (clear-everything) cases run the
// same traversal — the shapes differ by one lookup, and writing the traversal
// twice is how they would come to disagree.
static QJsonArray remapTurnShotIds(const QJsonArray& messages,
                                   const QHash<qint64, qint64>* shotIdMap,
                                   int& remapped, int& cleared)
{
    QJsonArray out;
    for (const QJsonValue& v : messages) {
        QJsonObject msg = v.toObject();
        if (!msg.contains(QStringLiteral("shotId"))) {
            // Never carried one (free-form turn, or a pre-shotId conversation).
            // Nothing to remap, and adding a key here would invent linkage.
            out.append(msg);
            continue;
        }

        const qint64 srcId = static_cast<qint64>(msg.value(QStringLiteral("shotId")).toDouble());
        const qint64 destId = shotIdMap ? shotIdMap->value(srcId, 0) : 0;
        if (destId > 0) {
            msg[QStringLiteral("shotId")] = static_cast<double>(destId);
            remapped++;
        } else {
            // Source shot did not come across (or no shots came at all).
            // REMOVE rather than zero: omission is this field's documented null
            // state, and `shotId: 0` would be a placeholder the readers do not
            // expect. Leaving srcId in place is the one thing that must not
            // happen — ids only climb, so it would eventually name a real,
            // unrelated shot.
            msg.remove(QStringLiteral("shotId"));
            cleared++;
        }
        out.append(msg);
    }
    return out;
}

QJsonArray AIConversation::exportConversationsStatic(AppSettings& settings)
{
    QJsonArray result;
    const QByteArray rawIndex = settings.value(QStringLiteral("ai/conversations/index")).toByteArray();
    if (rawIndex.isEmpty()) return result;

    const QJsonDocument indexDoc = QJsonDocument::fromJson(rawIndex);
    if (!indexDoc.isArray()) return result;

    for (const QJsonValue& v : indexDoc.array()) {
        const QJsonObject entry = v.toObject();
        const QString key = entry.value(QStringLiteral("key")).toString();
        if (key.isEmpty()) continue;

        const QString prefix = QStringLiteral("ai/conversations/") + key + QStringLiteral("/");
        QJsonObject conv;
        conv[QStringLiteral("key")] = key;
        conv[QStringLiteral("beanBrand")] = entry.value(QStringLiteral("beanBrand")).toString();
        conv[QStringLiteral("beanType")] = entry.value(QStringLiteral("beanType")).toString();
        conv[QStringLiteral("profileName")] = entry.value(QStringLiteral("profileName")).toString();
        // The package the thread belongs to. `equipmentId` is what the importer
        // re-keys through; `equipmentLabel` is the snapshot the index shows, and
        // travels with it so a restored thread is still nameable on a device
        // whose package rows are numbered differently.
        conv[QStringLiteral("equipmentId")] =
            entry.value(QStringLiteral("equipmentId")).toVariant().toLongLong();
        conv[QStringLiteral("equipmentLabel")] = entry.value(QStringLiteral("equipmentLabel")).toString();
        conv[QStringLiteral("indexTimestamp")] =
            entry.value(QStringLiteral("timestamp")).toVariant().toLongLong();
        conv[QStringLiteral("timestamp")] = settings.value(prefix + "timestamp").toString();
        conv[QStringLiteral("systemPrompt")] = settings.value(prefix + "systemPrompt").toString();
        conv[QStringLiteral("contextLabel")] = settings.value(prefix + "contextLabel").toString();

        const QByteArray messagesJson = settings.value(prefix + "messages").toByteArray();
        const QJsonDocument msgDoc = QJsonDocument::fromJson(messagesJson);
        conv[QStringLiteral("messages")] = msgDoc.isArray() ? msgDoc.array() : QJsonArray();

        result.append(conv);
    }
    return result;
}

AIConversation::ImportTally AIConversation::importConversationsStatic(
    AppSettings& settings,
    const QJsonArray& conversations,
    const QHash<qint64, qint64>* shotIdMap,
    const QHash<qint64, qint64>* equipmentIdMap)
{
    ImportTally tally;
    if (conversations.isEmpty()) return tally;

    // Existing index decides which keys are already here. An existing key is
    // SKIPPED whole, not merged — preserved from both original copies, which
    // agreed on this.
    QJsonArray index;
    const QByteArray rawIndex = settings.value(QStringLiteral("ai/conversations/index")).toByteArray();
    if (!rawIndex.isEmpty()) {
        const QJsonDocument doc = QJsonDocument::fromJson(rawIndex);
        if (doc.isArray()) index = doc.array();
    }
    QSet<QString> existingKeys;
    for (const QJsonValue& v : index)
        existingKeys.insert(v.toObject().value(QStringLiteral("key")).toString());

    int skippedExisting = 0;
    int malformed = 0;
    int foreignKey = 0;
    int unresolvedPackage = 0;
    for (const QJsonValue& val : conversations) {
        const QJsonObject conv = val.toObject();
        const QString srcKey = conv.value(QStringLiteral("key")).toString();
        // Two distinct outcomes that used to be one silent `continue`: a
        // damaged archive entry with no key at all, and a conversation this
        // device already has. Counting them separately is what stops "3
        // imported" from being the whole story when 40 arrived.
        // A conversation with no key, or whose `messages` is not an array, is a
        // damaged archive entry. The second shape used to import as an EMPTY
        // conversation and be reported as a success — a card that opens to
        // nothing. Both are counted locally and named in the log line below;
        // neither belongs on ImportTally, which no caller would read them from.
        if (srcKey.isEmpty() || !conv.value(QStringLiteral("messages")).isArray()) {
            malformed++;
            continue;
        }

        const QString beanBrand = conv.value(QStringLiteral("beanBrand")).toString();
        const QString beanType = conv.value(QStringLiteral("beanType")).toString();
        const QString profileName = conv.value(QStringLiteral("profileName")).toString();
        const qint64 srcEquipmentId =
            conv.value(QStringLiteral("equipmentId")).toVariant().toLongLong();

        // Re-derive the key the SOURCE should have had from the fields it sent.
        // A mismatch means the archive predates the package being part of the
        // key, so nothing on this device would ever open the thread — importing
        // it only occupies an index slot, which is what the one-time wipe of
        // pre-upgrade conversations already decided against.
        if (ConversationKey::derive(beanBrand, beanType, profileName, srcEquipmentId) != srcKey) {
            foreignKey++;
            continue;
        }

        // Renumber the package. 0 is the unpackaged pool and means the same
        // thing on both devices, so it passes through; anything else must be
        // found in the map. A miss is NOT demoted to 0 — that would file a
        // thread about one basket under "no basket", mixing it into every
        // unpackaged answer.
        qint64 destEquipmentId = 0;
        if (srcEquipmentId > 0) {
            destEquipmentId = equipmentIdMap ? equipmentIdMap->value(srcEquipmentId, 0) : 0;
            if (destEquipmentId <= 0) {
                unresolvedPackage++;
                continue;
            }
        }

        const QString key =
            ConversationKey::derive(beanBrand, beanType, profileName, destEquipmentId);

        // An existing key is skipped WHOLE, never merged: the copy on this
        // device is the live one and the archive's is older by construction.
        if (existingKeys.contains(key)) {
            skippedExisting++;
            continue;
        }

        const QJsonArray messages = remapTurnShotIds(
            conv.value(QStringLiteral("messages")).toArray(), shotIdMap,
            tally.turnsRemapped, tally.turnsCleared);

        const QString prefix = QStringLiteral("ai/conversations/") + key + QStringLiteral("/");
        settings.setValue(prefix + "systemPrompt", conv.value(QStringLiteral("systemPrompt")).toString());
        settings.setValue(prefix + "messages", QJsonDocument(messages).toJson(QJsonDocument::Compact));
        settings.setValue(prefix + "timestamp", conv.value(QStringLiteral("timestamp")).toString());
        settings.setValue(prefix + "contextLabel", conv.value(QStringLiteral("contextLabel")).toString());

        QJsonObject entry;
        entry[QStringLiteral("key")] = key;
        entry[QStringLiteral("beanBrand")] = beanBrand;
        entry[QStringLiteral("beanType")] = beanType;
        entry[QStringLiteral("profileName")] = profileName;
        // The DESTINATION package id, so the entry agrees with the key it is
        // filed under; the label rides along from the source, since this
        // device may not have named the package the same way.
        entry[QStringLiteral("equipmentId")] = destEquipmentId;
        entry[QStringLiteral("equipmentLabel")] = conv.value(QStringLiteral("equipmentLabel")).toString();
        entry[QStringLiteral("timestamp")] = conv.value(QStringLiteral("indexTimestamp")).toVariant().toLongLong();
        index.append(entry);
        existingKeys.insert(key);
        tally.conversationsImported++;
    }

    if (tally.conversationsImported > 0) {
        settings.setValue(QStringLiteral("ai/conversations/index"),
                          QJsonDocument(index).toJson(QJsonDocument::Compact));
    }

    qDebug() << "AIConversation::importConversationsStatic:" << tally.conversationsImported
             << "conversation(s) imported," << skippedExisting
             << "already present," << malformed << "malformed," << foreignKey
             << "keyed before equipment," << unresolvedPackage
             << "naming a package that did not come across;"
             << tally.turnsRemapped << "shot reference(s) remapped," << tally.turnsCleared
             << "cleared"
             << (shotIdMap ? "" : "(no shot import accompanied them — all ids cleared)");
    return tally;
}

int AIConversation::dropUnresolvableShotIds(QJsonArray& messages,
                                            const QSet<qint64>& existingShotIds)
{
    int dropped = 0;
    for (qsizetype i = 0; i < messages.size(); ++i) {
        QJsonObject msg = messages.at(i).toObject();
        if (!msg.contains(QStringLiteral("shotId"))) continue;
        const qint64 id = static_cast<qint64>(msg.value(QStringLiteral("shotId")).toDouble());
        if (id > 0 && existingShotIds.contains(id)) continue;
        msg.remove(QStringLiteral("shotId"));
        messages.replace(i, msg);
        dropped++;
    }
    return dropped;
}

void AIConversation::dropTrailingFailedUserTurn()
{
    // onAnalysisFailed keeps the failed user turn as the last entry so it can be
    // retried. A successful turn always ends on an assistant turn, so this only
    // fires in the failed state — it never discards a legitimate message.
    if (!m_messages.isEmpty() &&
        m_messages.last().toObject().value("role").toString() == QStringLiteral("user")) {
        m_messages.removeLast();
    }
}

void AIConversation::sendRequest()
{
    if (!m_aiManager || !m_aiManager->isConfigured()) {
        m_errorMessage = tr_("ai.error.notConfigured", "AI not configured");
        emit errorOccurred(m_errorMessage);
        return;
    }

    m_busy = true;
    emit busyChanged();
    emit canRetryChanged();

    trimHistory();

    qDebug() << "AIConversation: Sending request with" << m_messages.size() << "messages";
    m_aiManager->analyzeConversation(m_systemPrompt, m_messages);
}

void AIConversation::onAnalysisComplete(const QString& response)
{
    if (!m_busy) return;  // Not our request

    m_busy = false;
    m_lastResponse = response;

    // Parse the trailing fenced ```json block (issue #1054). When the
    // response makes a concrete recommendation, the model appends a
    // `nextShot` JSON object that we persist alongside the prose so
    // downstream callers (recentAdvice block #1053, future coachmark UI)
    // can read the structured prediction without re-parsing prose.
    addAssistantMessage(response, AIManager::parseStructuredNext(response));

    // Auto-save so conversation can be continued later
    saveToStorage();

    emit busyChanged();
    emit historyChanged();
    emit canRetryChanged();
    emit responseReceived(response);

    qDebug() << "AIConversation: Response received, history now has" << m_messages.size() << "messages";
}

void AIConversation::onAnalysisFailed(const QString& error)
{
    if (!m_busy) return;  // Not our request

    m_busy = false;
    m_errorMessage = error;

    // Keep the failed user turn in history so the user can retry it without
    // retyping (see openspec/changes/add-ai-advisor-retry). The turn is not
    // persisted — saveToStorage only runs on success — so a failed turn never
    // survives a reload.
    emit busyChanged();
    emit historyChanged();
    emit canRetryChanged();
    emit errorOccurred(error);

    qDebug() << "AIConversation: Request failed:" << error;
}

bool AIConversation::canRetry() const
{
    if (m_busy || m_messages.isEmpty())
        return false;
    return m_messages.last().toObject().value("role").toString() == QStringLiteral("user");
}

void AIConversation::retry()
{
    if (!canRetry()) {
        qDebug() << "AIConversation::retry ignored — no pending failed turn (busy:" << m_busy
                 << "messages:" << m_messages.size() << ")";
        return;
    }

    // Re-send the existing pending turn verbatim. Unlike followUp(), this does
    // not append a new user message and does not re-run the rating/metadata
    // capture hooks — those already fired when the turn was first submitted.
    m_errorMessage.clear();
    emit errorOccurred(m_errorMessage);
    sendRequest();
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
            // The turn IS a JSON object; the question and the shot label are its
            // own fields. This used to recover the question from the message text
            // by finding "Here's my latest shot:", taking the last blank line, and
            // guessing whether what followed "looked like a question" (no ": ",
            // under 500 characters) — a heuristic that existed only because the
            // question was concatenated around the payload.
            QJsonParseError perr{};
            const QJsonDocument pdoc = QJsonDocument::fromJson(content.toUtf8(), &perr);
            if (perr.error == QJsonParseError::NoError && pdoc.isObject()) {
                const QJsonObject turn = pdoc.object();
                const QString label = turn.value(QStringLiteral("shotLabel")).toString();
                const QString question = turn.value(QStringLiteral("question")).toString();
                const QString bev = turn.value(QStringLiteral("shot")).toObject()
                                        .value(QStringLiteral("beverageType")).toString();
                const bool isFilter =
                    bev.compare(QLatin1String("filter"), Qt::CaseInsensitive) == 0
                    || bev.compare(QLatin1String("pourover"), Qt::CaseInsensitive) == 0;
                if (!label.isEmpty())
                    text += isFilter ? "**[Coffee " + label + "]**" : "**[Shot " + label + "]**";
                else
                    text += isFilter ? "**[Coffee Data]**" : "**[Shot Data]**";
                if (!question.isEmpty()) text += "\n**You:** " + question;
            } else {
                // A free-form follow-up with no shot attached is sent as plain text.
                text += "**You:** " + content;
            }
        } else if (role == "assistant") {
            text += "**" + providerName() + ":** " + stripStructuredNextBlock(content);
        }
    }

    return text;
}

QJsonObject AIConversation::changesFromPreviousShot(const QString& shotLabel,
                                                    const QString& shotPayload) const
{
    // Find the previous shot in this conversation, excluding the current one so a
    // shot never diffs against itself.
    const PreviousShotInfo prev = findPreviousShot(shotLabel);
    if (prev.content.isEmpty() || prev.shotLabel.isEmpty()) return QJsonObject();

    // extractShotFields reads the structured envelope, falling back to the legacy
    // prose regex for turns stored before the payload became one JSON object.
    const ShotFields curr = extractShotFields(shotPayload);
    const ShotFields prevFields = extractShotFields(prev.content);

    QJsonObject changed;
    auto diffField = [&changed](const QString& a, const QString& b, const char* key) {
        if (a.isEmpty() || b.isEmpty() || a == b) return;
        changed[QLatin1String(key)] = QJsonObject{{"from", a}, {"to", b}};
    };
    diffField(prevFields.doseG, curr.doseG, "doseG");
    diffField(prevFields.yieldG, curr.yieldG, "yieldG");
    diffField(prevFields.durationSec, curr.durationSec, "durationSec");
    diffField(prevFields.grinder, curr.grinder, "grinder");

    // "Nothing changed" is a fact the model needs as much as a diff is — it is the
    // difference between "your change did nothing" and "you changed nothing".
    QJsonObject out;
    out["comparedToShot"] = prev.shotLabel;
    out["changed"] = changed;
    out["anyChange"] = !changed.isEmpty();
    return out;
}

QString AIConversation::multiShotSystemPrompt(const QString& beverageType, const QString& profileTitle,
                                               const QString& profileType, const QString& profileKbId)
{
    // Use the profile-aware system prompt (base + per-profile knowledge section)
    QString base = ShotSummarizer::shotAnalysisSystemPrompt(beverageType, profileTitle, profileType, profileKbId);
    base += QStringLiteral(
        "\n\n## Multi-Shot Context\n\n"
        "You are helping the user dial in across multiple shots in a single session. "
        "Track progress across shots and reference previous attempts to identify trends. "
        "Keep advice to ONE specific change per shot — don't overload with multiple adjustments.");
    return base;
}

QString AIConversation::stripStructuredNextBlock(const QString& content)
{
    // Strip the trailing ```json ... ``` block that the system prompt asks the
    // model to append when making a concrete parameter recommendation. The block
    // is parsed and stored as structuredNext on the message; it must not appear
    // in the displayed conversation text.
    QList<qsizetype> fences;
    qsizetype pos = 0;
    while (true) {
        pos = content.indexOf(QStringLiteral("```"), pos);
        if (pos < 0) break;
        fences.append(pos);
        pos += 3;
    }
    if (fences.size() < 2) return content;

    const qsizetype openerStart = fences.at(fences.size() - 2);
    const qsizetype closerStart = fences.at(fences.size() - 1);

    // Closer must be followed only by whitespace.
    for (qsizetype i = closerStart + 3; i < content.size(); ++i) {
        if (!content[i].isSpace()) return content;
    }

    // Opener tag must be "json".
    const qsizetype tagStart = openerStart + 3;
    const qsizetype nl = content.indexOf(QLatin1Char('\n'), tagStart);
    if (nl < 0 || nl >= closerStart) return content;
    if (content.mid(tagStart, nl - tagStart).trimmed().compare(QStringLiteral("json"), Qt::CaseInsensitive) != 0)
        return content;

    return content.left(openerStart).trimmed();
}


AIConversation::ShotFields AIConversation::extractShotFields(const QString& content)
{
    // Every stored user turn is one JSON object — the whole message, not an
    // object embedded in prose. This used to walk the string with a brace-depth
    // counter (skipping string literals and escapes) to find the envelope
    // between a "## Shot (date)" header and a trailing question, because "Qt's
    // JSON parser rejects trailing prose". Nothing writes that shape any more,
    // and clearAllConversationsOnce drops the turns that did.
    ShotFields fields;

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(content.toUtf8(), &err);
    if (err.error == QJsonParseError::NoError && doc.isObject()) {
        {
            const QJsonObject obj = doc.object();
            fields.shotLabel = obj.value(QStringLiteral("shotLabel")).toString();
            const QJsonObject shot = obj.value(QStringLiteral("shot")).toObject();
            const QJsonObject currentBean = obj.value(QStringLiteral("currentBean")).toObject();
            const QJsonObject profile = obj.value(QStringLiteral("profile")).toObject();

            // Numeric fields render with the same precision the original
            // regex captured ("18.0" / "36.0" / "30.0") so the legacy
            // diff strings ("Dose 18.0g→20.0g") read identically. JSON
            // fromJson() preserves doubles, so we format here.
            auto fmtNum = [](double v, int prec) {
                return QString::number(v, 'f', prec);
            };
            if (shot.contains(QStringLiteral("doseG")))
                fields.doseG = fmtNum(shot.value(QStringLiteral("doseG")).toDouble(), 1);
            else if (currentBean.contains(QStringLiteral("doseWeightG")))
                fields.doseG = fmtNum(currentBean.value(QStringLiteral("doseWeightG")).toDouble(), 1);

            if (shot.contains(QStringLiteral("yieldG")))
                fields.yieldG = fmtNum(shot.value(QStringLiteral("yieldG")).toDouble(), 1);
            if (shot.contains(QStringLiteral("durationSec")))
                fields.durationSec = fmtNum(shot.value(QStringLiteral("durationSec")).toDouble(), 0);
            if (shot.contains(QStringLiteral("enjoyment0to100")))
                fields.score = QString::number(shot.value(QStringLiteral("enjoyment0to100")).toInt());
            if (shot.contains(QStringLiteral("notes")))
                fields.notes = shot.value(QStringLiteral("notes")).toString();

            // Grinder string reproduces the legacy prose format
            // exactly: "<brand> <model> with <burrs> @ <setting>"
            // (see ShotSummarizer::renderShotAnalysisProse pre-#1041 —
            // the same format the s_grinderRe regex still captures from
            // stored conversations). Producing the same string on the
            // structured path keeps cross-format diffs (prev=legacy
            // regex, curr=structured) free of spurious "grinder
            // changed" diffs in conversations that span both eras.
            const QString gb = currentBean.value(QStringLiteral("grinderBrand")).toString();
            const QString gm = currentBean.value(QStringLiteral("grinderModel")).toString();
            const QString gbur = currentBean.value(QStringLiteral("grinderBurrs")).toString();
            const QString gs = shot.contains(QStringLiteral("grinderSetting"))
                ? shot.value(QStringLiteral("grinderSetting")).toString()
                : currentBean.value(QStringLiteral("grinderSetting")).toString();
            // RPM is the second half of the dial-in; fold it into the change-
            // detection string so an RPM-only move registers as "grinder changed".
            const int rpm = shot.contains(QStringLiteral("rpm"))
                ? shot.value(QStringLiteral("rpm")).toInt()
                : currentBean.value(QStringLiteral("rpm")).toInt();
            QString grinder;
            if (!gb.isEmpty() && !gm.isEmpty())
                grinder = gb + QLatin1Char(' ') + gm;
            else if (!gb.isEmpty())
                grinder = gb;
            else
                grinder = gm;
            if (!gbur.isEmpty()) grinder += QStringLiteral(" with ") + gbur;
            if (!gs.isEmpty()) grinder += QStringLiteral(" @ ") + gs;
            if (rpm > 0) grinder += QStringLiteral(" ") + QString::number(rpm) + QStringLiteral(" RPM");
            fields.grinder = grinder;

            fields.profileTitle = profile.value(QStringLiteral("title")).toString();

            // Detector flags: read by stable `kind` from the structured
            // `shot.detectorObservations[]` array (issue #1037). Each
            // entry's `kind` is a fixed enum the deterministic detector
            // sets (see ShotAnalysis::analyzeShot in
            // `src/ai/shotanalysis.cpp`) — robust against future
            // rewordings of the human-readable `text`.
            //
            // Two fallback layers, in order:
            //  1. Some envelopes ship `text` without `kind` (lines that
            //     predate #1037's kind annotation). Substring-match the
            //     production text strings against the per-line `text`.
            //  2. Older envelopes omit `detectorObservations[]` entirely.
            //     Substring-match the `shotAnalysis` prose body.
            //
            // The substring needles match the actual production strings
            // (case-insensitive). Earlier code looked for "Channeling
            // detected" (capital C), which the detector never emitted —
            // so the flag was always silently false. Fixing here corrects
            // both the structured-array fallback and the prose path.
            auto kindIsChanneling = [](const QString& kind) {
                return kind == QStringLiteral("channeling_sustained")
                    || kind == QStringLiteral("channeling_transient");
            };
            auto containsChannelingText = [](const QString& s) {
                return s.contains(QStringLiteral("channeling detected"),
                                  Qt::CaseInsensitive);
            };
            const QJsonArray observations = shot.value(
                QStringLiteral("detectorObservations")).toArray();
            if (!observations.isEmpty()) {
                for (const QJsonValue& v : observations) {
                    const QJsonObject obs = v.toObject();
                    const QString kind = obs.value(QStringLiteral("kind")).toString();
                    const QString text = obs.value(QStringLiteral("text")).toString();
                    if (!kind.isEmpty()) {
                        if (kindIsChanneling(kind))
                            fields.channelingDetected = true;
                    } else {
                        // Pre-#1037 entries without a `kind`: fall back
                        // to substring on the line's freeform `text`.
                        if (containsChannelingText(text))
                            fields.channelingDetected = true;
                    }
                }
            } else {
                const QString prose = obj.value(QStringLiteral("shotAnalysis")).toString();
                fields.channelingDetected = containsChannelingText(prose);
            }

            return fields;
        }
    }
    return fields;
}

AIConversation::PreviousShotInfo AIConversation::findPreviousShot(const QString& excludeLabel) const
{
    // Walk backwards to the most recent user turn that carried a shot, skipping
    // the one being excluded so a shot never diffs against itself. A turn carries
    // a shot when its payload has a `shotLabel` — a field read, where this used to
    // be two substring probes and a regex over the message text.
    for (qsizetype i = m_messages.size() - 1; i >= 0; i--) {
        const QJsonObject msg = m_messages[i].toObject();
        if (msg["role"].toString() != "user") continue;
        const QString content = msg["content"].toString();
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(content.toUtf8(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) continue;
        const QString label = doc.object().value(QStringLiteral("shotLabel")).toString();
        if (label.isEmpty()) continue;
        if (!excludeLabel.isEmpty() && label == excludeLabel) continue;
        return { content, label };
    }
    return {};
}

void AIConversation::saveToStorage()
{
    if (m_storageKey.isEmpty()) {
        if (!m_messages.isEmpty())
            qWarning() << "AIConversation::saveToStorage: storage key is empty but conversation has" << m_messages.size() << "messages — data not saved";
        return;
    }

    AppSettings settings;
    QString prefix = "ai/conversations/" + m_storageKey + "/";

    // Guard against AIConversation::appendAssistantTurnForKey (the MCP
    // ai_advisor_invoke path) having appended turns to this same key since
    // we last synced (loadFromStorage/saveToStorage). That helper does a
    // proper read-modify-write, but this method previously did a blind
    // overwrite from m_messages — if the two interleaved, whichever wrote
    // last silently erased the other's turn (found during manual
    // verification of fix-multishot-advice-tracking: a real, on-screen
    // response never made it into persisted storage).
    //
    // m_messages.size() minus m_unsyncedMessages.size() is what we expect
    // disk to currently hold — everything we last synced, before our own
    // still-pending additions. If disk actually holds MORE than that,
    // another writer added turns we don't know about; splice our pending
    // messages onto the real current disk contents instead of overwriting
    // them away. Comparing against m_unsyncedMessages (not a simple
    // "messages synced so far" counter) means this stays correct even when
    // trimHistory() has compacted m_messages's shape in the meantime.
    QJsonArray toWrite = m_messages;
    const qsizetype expectedPriorSize = m_messages.size() - m_unsyncedMessages.size();
    const QByteArray onDiskRaw = settings.value(prefix + "messages").toByteArray();
    if (!onDiskRaw.isEmpty()) {
        QJsonParseError err{};
        const QJsonDocument onDiskDoc = QJsonDocument::fromJson(onDiskRaw, &err);
        if (err.error == QJsonParseError::NoError && onDiskDoc.isArray()) {
            const QJsonArray onDisk = onDiskDoc.array();
            if (onDisk.size() > expectedPriorSize) {
                QJsonArray merged = onDisk;
                for (const QJsonValue& v : std::as_const(m_unsyncedMessages))
                    merged.append(v);
                toWrite = merged;
                // The disk copy predates this session's stale-id repair, so
                // adopting it verbatim brings the forgotten ids straight back —
                // which silently undid repairStaleTurnShotIds on exactly the
                // installs that have another writer, i.e. MCP ai_advisor_invoke.
                // Re-apply the drop to the reconciled array.
                if (!m_forgottenShotIds.isEmpty()) {
                    for (qsizetype i = 0; i < toWrite.size(); ++i) {
                        QJsonObject msg = toWrite.at(i).toObject();
                        if (!msg.contains(QStringLiteral("shotId"))) continue;
                        const qint64 id = static_cast<qint64>(
                            msg.value(QStringLiteral("shotId")).toDouble());
                        if (!m_forgottenShotIds.contains(id)) continue;
                        msg.remove(QStringLiteral("shotId"));
                        toWrite[i] = msg;
                    }
                }
                qDebug() << "AIConversation::saveToStorage: reconciled" << (onDisk.size() - expectedPriorSize)
                          << "message(s) appended by another writer for key:" << m_storageKey;
            }
        }
    }

    settings.setValue(prefix + "systemPrompt", m_systemPrompt);

    settings.setValue(prefix + "messages", QJsonDocument(toWrite).toJson(QJsonDocument::Compact));

    settings.setValue(prefix + "timestamp", QDateTime::currentDateTime().toString(Qt::ISODate));
    m_unsyncedMessages = QJsonArray();
    // Only adopt the reconciled array when it actually differs — avoids
    // undoing trimHistory()'s compaction of m_messages on the common path
    // where no other writer touched this key.
    if (toWrite.size() != m_messages.size())
        m_messages = toWrite;

    emit savedConversationChanged();
    qDebug() << "AIConversation: Saved conversation with" << m_messages.size() << "messages to key:" << m_storageKey;
}

void AIConversation::repairStaleTurnShotIds()
{
    // Forget turn shotIds that no longer name a shot.
    //
    // Conversations imported before the remap existed hold ids from the SOURCE
    // database. Those are not merely useless: shot ids only ever climb, so a
    // stale id eventually becomes a VALID id belonging to an unrelated shot,
    // and the write-back path would then act on the wrong one. Dropping them
    // closes that window for installs already carrying the damage — and it also
    // catches an id whose shot the user simply deleted.
    //
    // This is NOT undoable, and an earlier version of this comment claimed it
    // was. It mutates m_messages, and saveToStorage() persists the result — so
    // the next save of this conversation (a follow-up turn, or switching away
    // from it, which AIManager::switchConversation saves on) makes the drop
    // permanent. That is acceptable ONLY because the id is known not to
    // resolve; it is not acceptable on a guess, which is why the nullopt case
    // below leaves the data alone.
    //
    // Called from loadFromStorage AND from AIManager::setShotHistoryStorage:
    // the manager loads the most recent conversation in its own constructor,
    // before the storage is wired, so without the second call the conversation
    // the user is most likely to continue would load unrepaired on every launch.
    if (m_messages.isEmpty() || !m_aiManager || !m_aiManager->shotHistoryStorage()) {
        // Not an error — it just means we cannot check yet. setShotHistoryStorage
        // calls back here when it can. Logged because the silent version of this
        // branch is the one that actually fires in production.
        if (!m_messages.isEmpty()) {
            qDebug() << "AIConversation::repairStaleTurnShotIds: no shot storage yet for key"
                     << m_storageKey << "- deferring until it is wired";
        }
        return;
    }

    QSet<qint64> referenced;
    for (const QJsonValue& v : std::as_const(m_messages)) {
        const QJsonObject msg = v.toObject();
        if (!msg.contains(QStringLiteral("shotId"))) continue;
        const qint64 id = static_cast<qint64>(msg.value(QStringLiteral("shotId")).toDouble());
        if (id > 0) referenced.insert(id);
    }
    if (referenced.isEmpty()) return;

    const std::optional<QSet<qint64>> live =
        m_aiManager->shotHistoryStorage()->existingShotIds(referenced);
    if (!live) {
        // Could not answer — a not-ready database or a failed query. Deleting on
        // an unanswered question is the aggressive direction, not the
        // conservative one: one transient SQLITE_BUSY would strip every
        // advisor-to-shot link on the device, permanently, at the next save.
        qWarning() << "AIConversation::repairStaleTurnShotIds: could not check turn shot"
                   << "references for key" << m_storageKey << "- leaving them as they are";
        return;
    }

    // Capture the count BEFORE `referenced` is reduced to the unresolvable
    // ones below — logging it afterwards reported 0 while claiming they all
    // resolved, which is worse than not logging it.
    const qsizetype checked = referenced.size();

    // Remembered so saveToStorage can re-apply the drop after it reconciles
    // against another writer's copy on disk — that copy still holds the stale
    // ids, and adopting it verbatim silently undid this repair.
    for (qint64 id : std::as_const(*live)) referenced.remove(id);
    m_forgottenShotIds.unite(referenced);

    const int dropped = dropUnresolvableShotIds(m_messages, *live);
    if (dropped > 0) {
        qDebug() << "AIConversation::repairStaleTurnShotIds: dropped" << dropped
                 << "turn shot reference(s) that name no existing shot, for key"
                 << m_storageKey;
    } else {
        // Say so. The deferral above announces that this check is coming, and
        // without a line here the clean outcome is indistinguishable in a
        // submitted log from the check never having run at all — which is the
        // state this method was added to fix, so silence is the wrong default
        // for exactly the reader trying to confirm it works.
        qDebug() << "AIConversation::repairStaleTurnShotIds: all" << checked
                 << "distinct turn shot reference(s) resolve, for key" << m_storageKey;
    }
}

void AIConversation::loadFromStorage()
{
    if (m_storageKey.isEmpty()) return;

    AppSettings settings;
    QString prefix = "ai/conversations/" + m_storageKey + "/";

    m_systemPrompt = settings.value(prefix + "systemPrompt").toString();

    QByteArray messagesJson = settings.value(prefix + "messages").toByteArray();
    m_messages = QJsonArray();
    if (!messagesJson.isEmpty()) {
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(messagesJson, &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            qWarning() << "AIConversation::loadFromStorage: JSON parse error for key" << m_storageKey
                        << ":" << parseError.errorString();
            m_errorMessage = tr_("ai.error.loadHistoryFailed", "Could not load conversation history");
            emit errorOccurred(m_errorMessage);
        } else if (doc.isArray()) {
            m_messages = doc.array();
        } else {
            qWarning() << "AIConversation::loadFromStorage: Expected JSON array but got"
                        << (doc.isObject() ? "object" : "other") << "for key" << m_storageKey;
        }
    }

    // Forget turn shotIds that no longer name a shot.
    //
    // Conversations imported before the remap existed hold ids from the SOURCE
    // database. Those are not merely useless: shot ids only ever climb, so a
    // stale id eventually becomes a VALID id belonging to an unrelated shot,
    // and the write-back path would then act on the wrong one. Dropping them at
    // read time closes that window for installs already carrying the damage —
    // and it also catches an id whose shot the user simply deleted.
    //
    repairStaleTurnShotIds();

    // Update last response from the last assistant message
    m_lastResponse.clear();
    for (qsizetype i = m_messages.size() - 1; i >= 0; i--) {
        QJsonObject msg = m_messages[i].toObject();
        if (msg["role"].toString() == "assistant") {
            m_lastResponse = msg["content"].toString();
            break;
        }
    }
    m_unsyncedMessages = QJsonArray();

    emit historyChanged();
    emit canRetryChanged();
    emit savedConversationChanged();
    qDebug() << "AIConversation: Loaded conversation with" << m_messages.size() << "messages from key:" << m_storageKey;
}

bool AIConversation::hasSavedConversation() const
{
    if (m_storageKey.isEmpty()) return false;

    AppSettings settings;
    QString prefix = "ai/conversations/" + m_storageKey + "/";
    QByteArray messagesJson = settings.value(prefix + "messages").toByteArray();
    if (messagesJson.isEmpty()) return false;

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(messagesJson, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "AIConversation::hasSavedConversation: corrupted data for key" << m_storageKey;
        return false;
    }
    return doc.isArray() && !doc.array().isEmpty();
}

void AIConversation::trimHistory()
{
    // Keep last MAX_VERBATIM_PAIRS user+assistant pairs + the pending user message verbatim.
    // Older shot messages get summarized into a compact context block.
    // Older non-shot messages (plain follow-ups) are dropped.

    // Threshold: MAX_VERBATIM_PAIRS pairs = 2*MAX_VERBATIM_PAIRS messages, plus 1 pending user message
    int maxVerbatim = MAX_VERBATIM_PAIRS * 2 + 1;
    if (m_messages.size() <= maxVerbatim) return;

    // Split messages: everything before the last maxVerbatim are "old"
    int oldCount = static_cast<int>(m_messages.size()) - maxVerbatim;
    // Ensure oldCount lands on a pair boundary (even index) so verbatim
    // messages start with a user message — required for Gemini role alternation
    if (oldCount % 2 != 0) {
        oldCount++;
    }
    if (oldCount >= static_cast<int>(m_messages.size())) return;

    QStringList summaries;
    int droppedFollowUps = 0;

    for (int i = 0; i < oldCount; i++) {
        QJsonObject msg = m_messages[i].toObject();
        if (msg["role"].toString() == "user") {
            QString content = msg["content"].toString();
            QString summary = summarizeShotMessage(content);
            if (!summary.isEmpty()) {
                // Look ahead for the assistant response to include recommendation context
                if (i + 1 < oldCount) {
                    QJsonObject nextMsg = m_messages[i + 1].toObject();
                    if (nextMsg["role"].toString() == "assistant") {
                        QString advice = summarizeAdvice(nextMsg["content"].toString());
                        if (!advice.isEmpty()) {
                            summary += " → Advice: " + advice;
                        }
                    }
                }
                summaries.append(summary);
            } else {
                // A turn that carried a shot but produced no summary means the
                // payload shape moved without this code following it.
                if (content.trimmed().startsWith(QLatin1Char('{'))
                    && content.contains(QStringLiteral("\"shotLabel\""))) {
                    qWarning() << "AIConversation::trimHistory: Shot message could not be summarized, payload may have changed shape";
                }
                droppedFollowUps++;
            }
        }
    }

    // Build trimmed array
    QJsonArray trimmed;

    if (!summaries.isEmpty() || droppedFollowUps > 0) {
        // Prepend a summary context message
        QString summaryContent;
        if (!summaries.isEmpty()) {
            summaryContent = "Previous shots summary:\n" + summaries.join("\n");
        }
        if (droppedFollowUps > 0) {
            if (!summaryContent.isEmpty()) summaryContent += "\n";
            summaryContent += QString("(%1 earlier follow-up message(s) omitted for brevity)").arg(droppedFollowUps);
        }

        QJsonObject summaryMsg;
        summaryMsg["role"] = QString("user");
        summaryMsg["content"] = summaryContent;
        trimmed.append(summaryMsg);

        // Add a synthetic assistant acknowledgment
        QJsonObject ackMsg;
        ackMsg["role"] = QString("assistant");
        ackMsg["content"] = QString("Got it, I have context from your previous shots and messages. Let's continue.");
        trimmed.append(ackMsg);
    }

    // Append the verbatim recent messages
    for (int i = oldCount; i < m_messages.size(); i++) {
        trimmed.append(m_messages[i]);
    }

    int removed = static_cast<int>(m_messages.size()) - static_cast<int>(trimmed.size());
    m_messages = trimmed;

    if (removed > 0) {
        qDebug() << "AIConversation: Trimmed history, removed" << removed << "messages,"
                 << summaries.size() << "shots summarized," << m_messages.size() << "messages remaining";
    }
}

QString AIConversation::summarizeShotMessage(const QString& content)
{
    // A turn carries a shot when its payload has a `shotLabel`. extractShotFields
    // returns an empty label for a free-form follow-up, which is the guard.
    const ShotFields fields = extractShotFields(content);
    if (fields.shotLabel.isEmpty()) return QString();

    QString summary = "- Shot";
    if (!fields.shotLabel.isEmpty()) summary += " (" + fields.shotLabel + ")";
    summary += ":";
    if (!fields.profileTitle.isEmpty()) summary += " \"" + fields.profileTitle + "\"";
    if (!fields.doseG.isEmpty() && !fields.yieldG.isEmpty())
        summary += " " + fields.doseG + "g\u2192" + fields.yieldG + "g";
    if (!fields.durationSec.isEmpty()) summary += ", " + fields.durationSec + "s";
    if (!fields.grinder.isEmpty()) {
        QString truncGrinder = fields.grinder.length() > 30
            ? fields.grinder.left(27) + "..." : fields.grinder;
        summary += ", " + truncGrinder;
    }
    if (!fields.score.isEmpty()) summary += ", " + fields.score + "/100";
    if (!fields.notes.isEmpty()) {
        QString truncated = fields.notes.length() > 40
            ? fields.notes.left(37) + "..." : fields.notes;
        summary += ", \"" + truncated + "\"";
    }
    if (fields.channelingDetected) summary += " [channeling]";

    return summary;
}

QString AIConversation::summarizeAdvice(const QString& response)
{
    // Extract the first actionable sentence from the AI's response.
    // Look for common recommendation patterns.
    // We take the first sentence that contains an action verb related to espresso dialing.

    // Try to find a line that starts with a recommendation keyword
    static const QRegularExpression recRe("(?:^|\\n)\\s*(?:[-•*]\\s*)?(?:Try|Adjust|Grind|Increase|Decrease|Lower|Raise|Change|Move|Use|Reduce|Extend|Shorten)\\s[^\\n]{5,}",
                                          QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch m = recRe.match(response);
    if (m.hasMatch()) {
        QString advice = m.captured(0).trimmed();
        // Strip leading bullet markers
        if (advice.startsWith('-') || advice.startsWith(QChar(0x2022)) || advice.startsWith('*')) {
            advice = advice.mid(1).trimmed();
        }
        // Truncate to keep compact
        if (advice.length() > 80) advice = advice.left(77) + "...";
        return advice;
    }

    return QString();
}
