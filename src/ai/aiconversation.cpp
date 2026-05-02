#include "aiconversation.h"
#include "aimanager.h"
#include "shotsummarizer.h"

#include <QDateTime>
#include <QDebug>
#include <QSettings>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QRegularExpression>

// Outer wrapper regex for the "## Shot (date)" header that
// `addShotContext` prepends OUTSIDE the JSON envelope. The header is
// not part of the envelope itself, so it stays a regex match. All
// per-shot data fields (dose / yield / duration / grinder / score /
// notes / profile) come from structured JSON fields now — see
// extractShotFields() below. Issue #1039.
const QRegularExpression AIConversation::s_shotLabelRe("## Shot \\(([^)]+)\\)");

// Legacy fallback regexes for stored conversations whose user messages
// were saved before issue #1034 introduced the JSON envelope and #1039
// added the structured `shot` block. These run only when JSON parsing
// fails AND the message looks like prose. New code should NOT add new
// callers — read `shot.*` / `currentBean.*` / `profile.*` / `tastingFeedback.*`
// from the parsed envelope instead.
const QRegularExpression AIConversation::s_doseRe("\\*\\*Dose\\*\\*:\\s*([\\d.]+)g");
const QRegularExpression AIConversation::s_yieldRe("\\*\\*Yield\\*\\*:\\s*([\\d.]+)g");
const QRegularExpression AIConversation::s_durationRe("\\*\\*Duration\\*\\*:\\s*([\\d.]+)s");
const QRegularExpression AIConversation::s_grinderRe("\\*\\*Grinder\\*\\*:\\s*(.+?)\\n");
const QRegularExpression AIConversation::s_profileRe("\\*\\*Profile\\*\\*:\\s*(.+?)(?:\\s*\\(by|\\n|$)");
const QRegularExpression AIConversation::s_scoreRe("\\*\\*Score\\*\\*:\\s*(\\d+)");
const QRegularExpression AIConversation::s_notesRe("\\*\\*Notes\\*\\*:\\s*\"([^\"]+)\"");

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
        m_errorMessage = "AI not available";
        emit errorOccurred(m_errorMessage);
        return;
    }
    if (m_busy) {
        qDebug() << "AIConversation::ask ignored — already busy";
        return;
    }

    // Clear previous conversation and start fresh
    m_messages = QJsonArray();
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
        m_errorMessage = "AI not available";
        emit errorOccurred(m_errorMessage);
        return false;
    }
    if (m_busy) {
        qDebug() << "AIConversation::followUp ignored — already busy";
        return false;
    }
    if (m_systemPrompt.isEmpty()) {
        qWarning() << "AIConversation::followUp called without prior ask()";
        m_errorMessage = "Please start a new conversation first";
        emit errorOccurred(m_errorMessage);
        return false;
    }

    m_errorMessage.clear();

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
        QSettings settings;
        QString prefix = "ai/conversations/" + m_storageKey + "/";
        settings.remove(prefix + "systemPrompt");
        settings.remove(prefix + "messages");
        settings.remove(prefix + "timestamp");
    }

    m_messages = QJsonArray();
    m_systemPrompt.clear();
    m_lastResponse.clear();
    m_errorMessage.clear();

    emit historyChanged();
    emit savedConversationChanged();
    qDebug() << "AIConversation: History cleared for key:" << m_storageKey;
}

void AIConversation::resetInMemory()
{
    m_messages = QJsonArray();
    m_systemPrompt.clear();
    m_lastResponse.clear();
    m_errorMessage.clear();
    emit historyChanged();
}

void AIConversation::setStorageKey(const QString& key)
{
    m_storageKey = key;
}

void AIConversation::setContextLabel(const QString& brand, const QString& type, const QString& profile)
{
    QStringList parts;
    QString bean;
    if (!brand.isEmpty() && !type.isEmpty())
        bean = brand + " " + type;
    else if (!brand.isEmpty())
        bean = brand;
    else if (!type.isEmpty())
        bean = type;

    if (!bean.isEmpty()) parts << bean;
    if (!profile.isEmpty()) parts << profile;

    m_contextLabel = parts.join(" / ");
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
    QSettings settings;
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
    QSettings settings;
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

void AIConversation::sendRequest()
{
    if (!m_aiManager || !m_aiManager->isConfigured()) {
        m_errorMessage = "AI not configured";
        emit errorOccurred(m_errorMessage);
        return;
    }

    m_busy = true;
    emit busyChanged();

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
                qsizetype shotStart = content.indexOf("Here's my latest shot:");
                if (shotStart >= 0) {
                    // Skip past "Here's my latest shot:\n\n"
                    qsizetype dataStart = content.indexOf("\n\n", shotStart);
                    if (dataStart >= 0) {
                        dataStart += 2;
                        // Find the end of shot data (look for pattern break)
                        // Shot data lines have format "Key: Value" or are part of structured sections
                        // The user's question is free-form text after the data

                        // Simple heuristic: find last "\n\n" and check if what follows
                        // looks like a question (doesn't contain ":" in typical key: value pattern)
                        qsizetype lastBreak = content.lastIndexOf("\n\n");
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

                // Format: [Shot date] or [Coffee date] depending on beverage type
                bool isFilter = content.contains("Beverage type**: filter", Qt::CaseInsensitive) ||
                               content.contains("Beverage type**: pourover", Qt::CaseInsensitive);

                // Extract shot label from "## Shot (date)" prefix if present
                QRegularExpressionMatch shotNumMatch = s_shotLabelRe.match(content);
                if (shotNumMatch.hasMatch()) {
                    QString label = shotNumMatch.captured(1);
                    text += isFilter ? "**[Coffee " + label + "]**" : "**[Shot " + label + "]**";
                } else {
                    text += isFilter ? "**[Coffee Data]**" : "**[Shot Data]**";
                }
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

void AIConversation::addShotContext(const QString& shotSummary, const QString& shotLabel,
                                     const QString& beverageType, const QString& profileTitle,
                                     const QString& profileType, const QString& profileKbId)
{
    if (m_busy) {
        qWarning() << "AIConversation::addShotContext ignored — already busy";
        m_errorMessage = "Please wait for the current request to complete";
        emit errorOccurred(m_errorMessage);
        return;
    }

    // If no existing conversation, set up the system prompt based on beverage type + profile
    if (m_systemPrompt.isEmpty()) {
        m_systemPrompt = multiShotSystemPrompt(beverageType, profileTitle, profileType, profileKbId);
    }

    // Add the new shot as context with its date/time label
    QString contextMessage = "## Shot (" + shotLabel + ")" +
                            "\n\nHere's my latest shot:\n\n" + shotSummary +
                            "\n\nPlease analyze this shot and provide recommendations, considering any previous shots we've discussed.";
    addUserMessage(contextMessage);
    sendRequest();

    emit historyChanged();
    qDebug() << "AIConversation: Added new shot context, now have" << m_messages.size() << "messages";
}

QString AIConversation::processShotForConversation(const QString& shotSummary, const QString& shotLabel)
{
    QString processed = shotSummary;

    // Find previous shot in conversation (exclude the current shot to avoid self-comparison)
    PreviousShotInfo prev = findPreviousShot(shotLabel);
    QString prevContent = prev.content;
    QString prevLabel = prev.shotLabel;

    if (!prevContent.isEmpty()) {
        // === Change detection ===
        // Read shot-VARIABLE fields directly from the JSON envelope
        // (issue #1039). Falls back to legacy prose regex automatically
        // when either message predates the JSON envelope.
        const ShotFields curr = extractShotFields(processed);
        const ShotFields prev = extractShotFields(prevContent);

        QStringList changes;

        auto diffField = [&](const QString& a, const QString& b,
                             const QString& label, const QString& unit) {
            if (!a.isEmpty() && !b.isEmpty() && a != b)
                changes << QString("%1 %2%3\u2192%4%5")
                    .arg(label, a, unit, b, unit);
        };
        diffField(prev.doseG, curr.doseG, QStringLiteral("Dose"), QStringLiteral("g"));
        diffField(prev.yieldG, curr.yieldG, QStringLiteral("Yield"), QStringLiteral("g"));
        diffField(prev.durationSec, curr.durationSec, QStringLiteral("Duration"), QStringLiteral("s"));
        // Grinder diff string keeps a different separator (" \u2192 " with
        // spaces) for legibility \u2014 the grinder string can be long
        // ("Niche Zero (63mm conical) at 4.0").
        if (!prev.grinder.isEmpty() && !curr.grinder.isEmpty() && prev.grinder != curr.grinder)
            changes << "Grinder " + prev.grinder + " \u2192 " + curr.grinder;

        // Prepend changes section
        QString changesSection;
        if (!prevLabel.isEmpty()) {
            if (!changes.isEmpty()) {
                changesSection = "**Changes from Shot (" + prevLabel + ")**: " + changes.join(", ") + "\n\n";
            } else {
                changesSection = "**No parameter changes from Shot (" + prevLabel + ")**\n\n";
            }
        }

        if (!changesSection.isEmpty()) {
            processed = changesSection + processed;
        }
    }

    return processed;
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

QString AIConversation::extractShotProse(const QString& content)
{
    // Cheap pre-check: if the trimmed content doesn't look like a JSON object,
    // skip the parse. Avoids QJsonDocument::fromJson on every legacy prose
    // message where it would fail and we'd ignore the result anyway.
    const QString trimmed = content.trimmed();
    if (!trimmed.startsWith(QLatin1Char('{'))) return content;

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(content.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return content;

    const QJsonObject obj = doc.object();
    if (!obj.contains(QStringLiteral("shotAnalysis"))) return content;
    return obj.value(QStringLiteral("shotAnalysis")).toString();
}

AIConversation::ShotFields AIConversation::extractShotFields(const QString& content)
{
    // Try the structured path first: the user message is the JSON
    // envelope `ShotSummarizer::buildUserPromptObject` produces. Each
    // numeric / string field is read from its canonical structured
    // location — `shot.*` for shot-VARIABLE values (dose / yield /
    // duration / score / notes), `currentBean.*` for grinder identity,
    // `profile.title` for profile name. The shot header label remains a
    // regex match against the OUTER message wrapper (it's not part of
    // the envelope — `addShotContext` prepends it).
    //
    // The user message is shaped by `addShotContext` as:
    //   "## Shot (label)\n\nHere's my latest shot:\n\n<json>\n\nPlease analyze..."
    // so the JSON object lives *between* the header and the trailing
    // user prompt. Find the first `{` and parse from there.
    ShotFields fields;

    QRegularExpressionMatch labelMatch = s_shotLabelRe.match(content);
    if (labelMatch.hasMatch()) fields.shotLabel = labelMatch.captured(1);

    // Locate the JSON envelope inside the message body. The message is
    // shaped as "## Shot (..)\n\nHere's my latest shot:\n\n<json>\n\n
    // Please analyze..." so we need to find the matching `}` for the
    // first `{` — Qt's JSON parser rejects trailing prose. Walk the
    // string with a depth counter, skipping over string literals.
    auto findJsonObject = [](const QString& s) -> QString {
        const qsizetype start = s.indexOf(QLatin1Char('{'));
        if (start < 0) return QString();
        int depth = 0;
        bool inString = false;
        bool escaped = false;
        for (qsizetype i = start; i < s.size(); ++i) {
            const QChar c = s[i];
            if (inString) {
                if (escaped) { escaped = false; continue; }
                if (c == QLatin1Char('\\')) { escaped = true; continue; }
                if (c == QLatin1Char('"')) inString = false;
                continue;
            }
            if (c == QLatin1Char('"')) { inString = true; continue; }
            if (c == QLatin1Char('{')) ++depth;
            else if (c == QLatin1Char('}')) {
                --depth;
                if (depth == 0) return s.mid(start, i - start + 1);
            }
        }
        return QString();
    };
    const QString jsonText = findJsonObject(content);
    if (!jsonText.isEmpty()) {
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(
            jsonText.toUtf8(), &err);
        if (err.error == QJsonParseError::NoError && doc.isObject()) {
            const QJsonObject obj = doc.object();
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
            QString grinder;
            if (!gb.isEmpty() && !gm.isEmpty())
                grinder = gb + QLatin1Char(' ') + gm;
            else if (!gb.isEmpty())
                grinder = gb;
            else
                grinder = gm;
            if (!gbur.isEmpty()) grinder += QStringLiteral(" with ") + gbur;
            if (!gs.isEmpty()) grinder += QStringLiteral(" @ ") + gs;
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
            // detected" (capital C) and "Temperature unstable", neither of
            // which the detector ever emitted — so detector flags were
            // always silently false. Fixing here corrects both the
            // structured-array fallback and the prose path.
            auto kindIsChanneling = [](const QString& kind) {
                return kind == QStringLiteral("channeling_sustained")
                    || kind == QStringLiteral("channeling_transient");
            };
            auto containsChannelingText = [](const QString& s) {
                return s.contains(QStringLiteral("channeling detected"),
                                  Qt::CaseInsensitive);
            };
            auto containsTempInstabilityText = [](const QString& s) {
                return s.contains(QStringLiteral("Temperature drifted"),
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
                        if (kind == QStringLiteral("temperature_drift"))
                            fields.temperatureUnstable = true;
                    } else {
                        // Pre-#1037 entries without a `kind`: fall back
                        // to substring on the line's freeform `text`.
                        if (containsChannelingText(text))
                            fields.channelingDetected = true;
                        if (containsTempInstabilityText(text))
                            fields.temperatureUnstable = true;
                    }
                }
            } else {
                const QString prose = obj.value(QStringLiteral("shotAnalysis")).toString();
                fields.channelingDetected = containsChannelingText(prose);
                fields.temperatureUnstable = containsTempInstabilityText(prose);
            }

            fields.fromStructuredEnvelope = true;
            return fields;
        }
    }

    // Legacy fallback: stored conversations whose user messages were
    // saved before #1034 / #1039 — the body is plain prose. Run the
    // legacy regexes against the (already extracted) prose.
    const QString prose = extractShotProse(content);
    auto extract = [&prose](const QRegularExpression& re) {
        QRegularExpressionMatch m = re.match(prose);
        return m.hasMatch() ? m.captured(1).trimmed() : QString();
    };
    fields.doseG = extract(s_doseRe);
    fields.yieldG = extract(s_yieldRe);
    fields.durationSec = extract(s_durationRe);
    fields.grinder = extract(s_grinderRe);
    fields.profileTitle = extract(s_profileRe);
    fields.score = extract(s_scoreRe);
    fields.notes = extract(s_notesRe);
    // Same actual production strings the structured path matches above.
    fields.channelingDetected = prose.contains(
        QStringLiteral("channeling detected"), Qt::CaseInsensitive);
    fields.temperatureUnstable = prose.contains(
        QStringLiteral("Temperature drifted"), Qt::CaseInsensitive);
    fields.fromStructuredEnvelope = false;
    return fields;
}

AIConversation::PreviousShotInfo AIConversation::findPreviousShot(const QString& excludeLabel) const
{
    // Walk backwards to find the most recent user message containing shot data,
    // excluding the shot with the given label to avoid self-comparison
    for (qsizetype i = m_messages.size() - 1; i >= 0; i--) {
        QJsonObject msg = m_messages[i].toObject();
        if (msg["role"].toString() == "user") {
            QString content = msg["content"].toString();
            if (content.contains("Shot Summary") || content.contains("Here's my latest shot")) {
                QRegularExpressionMatch match = s_shotLabelRe.match(content);
                QString label = match.hasMatch() ? match.captured(1) : QString();
                // Skip if this is the shot we're excluding
                if (!excludeLabel.isEmpty() && label == excludeLabel) {
                    continue;
                }
                return { content, label };
            }
        }
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

    QSettings settings;
    QString prefix = "ai/conversations/" + m_storageKey + "/";

    settings.setValue(prefix + "systemPrompt", m_systemPrompt);

    QJsonDocument doc(m_messages);
    settings.setValue(prefix + "messages", doc.toJson(QJsonDocument::Compact));

    settings.setValue(prefix + "timestamp", QDateTime::currentDateTime().toString(Qt::ISODate));

    emit savedConversationChanged();
    qDebug() << "AIConversation: Saved conversation with" << m_messages.size() << "messages to key:" << m_storageKey;
}

void AIConversation::loadFromStorage()
{
    if (m_storageKey.isEmpty()) return;

    QSettings settings;
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
            m_errorMessage = "Could not load conversation history";
            emit errorOccurred(m_errorMessage);
        } else if (doc.isArray()) {
            m_messages = doc.array();
        } else {
            qWarning() << "AIConversation::loadFromStorage: Expected JSON array but got"
                        << (doc.isObject() ? "object" : "other") << "for key" << m_storageKey;
        }
    }

    // Update last response from the last assistant message
    m_lastResponse.clear();
    for (qsizetype i = m_messages.size() - 1; i >= 0; i--) {
        QJsonObject msg = m_messages[i].toObject();
        if (msg["role"].toString() == "assistant") {
            m_lastResponse = msg["content"].toString();
            break;
        }
    }

    emit historyChanged();
    emit savedConversationChanged();
    qDebug() << "AIConversation: Loaded conversation with" << m_messages.size() << "messages from key:" << m_storageKey;
}

bool AIConversation::hasSavedConversation() const
{
    if (m_storageKey.isEmpty()) return false;

    QSettings settings;
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
                // Check if this looks like a shot message that we failed to summarize
                if (content.contains("Shot Summary") || content.contains("Here's my latest shot")) {
                    qWarning() << "AIConversation::trimHistory: Shot message could not be summarized, metrics may have changed format";
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
    // Quick "is this a shot message?" guard. Both the JSON envelope and
    // the legacy prose carry one of these substrings: the envelope's
    // `shotAnalysis` field includes "## Shot Summary"; `addShotContext`
    // prepends "Here's my latest shot:" to every per-shot user message.
    if (!content.contains("Shot Summary") && !content.contains("Here's my latest shot"))
        return QString();

    // Read all per-shot fields from the JSON envelope (#1039). The
    // legacy regex path fires automatically inside extractShotFields
    // when the message has no parseable JSON.
    const ShotFields fields = extractShotFields(content);

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
    if (fields.temperatureUnstable) summary += " [temp unstable]";

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
