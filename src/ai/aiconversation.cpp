#include "aiconversation.h"
#include "aimanager.h"
#include "shotsummarizer.h"

#include <QDebug>
#include <QSettings>
#include <QJsonDocument>
#include <QRegularExpression>

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

    trimHistory();

    qDebug() << "AIConversation: Sending request with" << m_messages.size() << "messages";
    m_aiManager->analyzeConversation(m_systemPrompt, m_messages);
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

                // Format: [Shot #N] or [Coffee #N] depending on beverage type
                bool isFilter = content.contains("Beverage type**: filter", Qt::CaseInsensitive) ||
                               content.contains("Beverage type**: pourover", Qt::CaseInsensitive);

                // Extract shot number from "## Shot #N" prefix if present
                QRegularExpression shotNumRe("^## Shot #(\\d+)");
                QRegularExpressionMatch shotNumMatch = shotNumRe.match(content);
                if (shotNumMatch.hasMatch()) {
                    QString num = shotNumMatch.captured(1);
                    text += isFilter ? "**[Coffee #" + num + "]**" : "**[Shot #" + num + "]**";
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

void AIConversation::addShotContext(const QString& shotSummary, int shotId, const QString& beverageType)
{
    if (m_busy) return;

    // If no existing conversation, set up the system prompt based on beverage type
    if (m_systemPrompt.isEmpty()) {
        m_systemPrompt = multiShotSystemPrompt(beverageType);
    }

    // Add the new shot as context with the app's shot ID
    QString contextMessage = "## Shot #" + QString::number(shotId) +
                            "\n\nHere's my latest shot:\n\n" + shotSummary +
                            "\n\nPlease analyze this shot and provide recommendations, considering any previous shots we've discussed.";
    addUserMessage(contextMessage);
    sendRequest();

    emit historyChanged();
    qDebug() << "AIConversation: Added new shot context, now have" << m_messages.size() << "messages";
}

QString AIConversation::processShotForConversation(const QString& shotSummary, int shotId)
{
    QString processed = shotSummary;

    // Find previous shot in conversation
    QString prevContent = findPreviousShotMessage();
    int prevShotId = findPreviousShotId();

    if (!prevContent.isEmpty()) {
        // === Recipe dedup: skip recipe if same profile ===
        QString newProfile = extractMetric(processed, "\\*\\*Profile\\*\\*:\\s*(.+?)(?:\\s*\\(by|\\n|$)");
        QString prevProfile = extractMetric(prevContent, "\\*\\*Profile\\*\\*:\\s*(.+?)(?:\\s*\\(by|\\n|$)");

        if (!newProfile.isEmpty() && newProfile == prevProfile) {
            // Remove the "## Profile Recipe" section — it's identical
            QRegularExpression recipeRe("## Profile Recipe[^\\n]*\\n(?:(?!## ).+\\n)*\\n?");
            processed.replace(recipeRe, "(Same profile recipe as previous shot)\n\n");
        }

        // === Change detection ===
        QStringList changes;

        QString newDose = extractMetric(processed, "\\*\\*Dose\\*\\*:\\s*([\\d.]+)g");
        QString prevDose = extractMetric(prevContent, "\\*\\*Dose\\*\\*:\\s*([\\d.]+)g");
        if (!newDose.isEmpty() && !prevDose.isEmpty() && newDose != prevDose)
            changes << "Dose " + prevDose + "g\u2192" + newDose + "g";

        QString newYield = extractMetric(processed, "\\*\\*Yield\\*\\*:\\s*([\\d.]+)g");
        QString prevYield = extractMetric(prevContent, "\\*\\*Yield\\*\\*:\\s*([\\d.]+)g");
        if (!newYield.isEmpty() && !prevYield.isEmpty() && newYield != prevYield)
            changes << "Yield " + prevYield + "g\u2192" + newYield + "g";

        QString newGrinder = extractMetric(processed, "\\*\\*Grinder\\*\\*:\\s*(.+?)\\n");
        QString prevGrinder = extractMetric(prevContent, "\\*\\*Grinder\\*\\*:\\s*(.+?)\\n");
        if (!newGrinder.isEmpty() && !prevGrinder.isEmpty() && newGrinder != prevGrinder)
            changes << "Grinder " + prevGrinder + " \u2192 " + newGrinder;

        QString newDuration = extractMetric(processed, "\\*\\*Duration\\*\\*:\\s*([\\d.]+)s");
        QString prevDuration = extractMetric(prevContent, "\\*\\*Duration\\*\\*:\\s*([\\d.]+)s");
        if (!newDuration.isEmpty() && !prevDuration.isEmpty() && newDuration != prevDuration)
            changes << "Duration " + prevDuration + "s\u2192" + newDuration + "s";

        // Prepend changes section
        QString changesSection;
        if (prevShotId > 0) {
            if (!changes.isEmpty()) {
                changesSection = "**Changes from Shot #" + QString::number(prevShotId) + "**: " + changes.join(", ") + "\n\n";
            } else {
                changesSection = "**No parameter changes from Shot #" + QString::number(prevShotId) + "**\n\n";
            }
        }

        if (!changesSection.isEmpty()) {
            processed = changesSection + processed;
        }
    }

    return processed;
}

QString AIConversation::multiShotSystemPrompt(const QString& beverageType)
{
    // Use the full single-shot system prompt (has all patterns, guidelines, target vs limiter explanation)
    QString base = ShotSummarizer::systemPrompt(beverageType);
    base += QStringLiteral(
        "\n\n## Multi-Shot Context\n\n"
        "You are helping the user dial in across multiple shots in a single session. "
        "Track progress across shots and reference previous attempts to identify trends. "
        "When the same profile is used across shots, focus on what changed (grind, dose, temperature) and how it affected the outcome. "
        "When the profile recipe is marked as 'same as previous shot', don't re-explain the profile — focus on differences in execution and results. "
        "Keep advice to ONE specific change per shot — don't overload with multiple adjustments.");
    return base;
}

QString AIConversation::extractMetric(const QString& content, const QString& pattern)
{
    QRegularExpression re(pattern);
    QRegularExpressionMatch match = re.match(content);
    return match.hasMatch() ? match.captured(1).trimmed() : QString();
}

QString AIConversation::findPreviousShotMessage() const
{
    // Walk backwards to find the most recent user message containing shot data
    for (int i = m_messages.size() - 1; i >= 0; i--) {
        QJsonObject msg = m_messages[i].toObject();
        if (msg["role"].toString() == "user") {
            QString content = msg["content"].toString();
            if (content.contains("Shot Summary") || content.contains("Here's my latest shot")) {
                return content;
            }
        }
    }
    return QString();
}

int AIConversation::findPreviousShotId() const
{
    // Walk backwards to find the most recent shot ID
    QRegularExpression shotIdRe("## Shot #(\\d+)");
    for (int i = m_messages.size() - 1; i >= 0; i--) {
        QJsonObject msg = m_messages[i].toObject();
        if (msg["role"].toString() == "user") {
            QRegularExpressionMatch match = shotIdRe.match(msg["content"].toString());
            if (match.hasMatch()) {
                return match.captured(1).toInt();
            }
        }
    }
    return 0;
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
    // Detect shot messages by content markers
    if (!content.contains("Shot Summary") && !content.contains("Here's my latest shot"))
        return QString();

    // Extract shot number from "## Shot #N" prefix
    QString shotNum;
    QRegularExpression shotNumRe("## Shot #(\\d+)");
    QRegularExpressionMatch numMatch = shotNumRe.match(content);
    if (numMatch.hasMatch()) {
        shotNum = numMatch.captured(1);
    }

    // Extract key metrics using regex
    QRegularExpression doseRe("\\*\\*Dose\\*\\*:\\s*([\\d.]+)g");
    QRegularExpression yieldRe("\\*\\*Yield\\*\\*:\\s*([\\d.]+)g");
    QRegularExpression durationRe("\\*\\*Duration\\*\\*:\\s*([\\d.]+)s");
    QRegularExpression scoreRe("\\*\\*Score\\*\\*:\\s*(\\d+)");
    QRegularExpression notesRe("\\*\\*Notes\\*\\*:\\s*\"([^\"]+)\"");

    QString dose, yield, duration, score, notes;

    QRegularExpressionMatch m = doseRe.match(content);
    if (m.hasMatch()) dose = m.captured(1);
    m = yieldRe.match(content);
    if (m.hasMatch()) yield = m.captured(1);
    m = durationRe.match(content);
    if (m.hasMatch()) duration = m.captured(1);
    m = scoreRe.match(content);
    if (m.hasMatch()) score = m.captured(1);
    m = notesRe.match(content);
    if (m.hasMatch()) notes = m.captured(1);

    // Extract profile name
    QRegularExpression profileRe("\\*\\*Profile\\*\\*:\\s*(.+?)(?:\\s*\\(by|\\n|$)");
    QRegularExpressionMatch pm = profileRe.match(content);
    QString profile = pm.hasMatch() ? pm.captured(1).trimmed() : QString();

    // Extract grinder info
    QRegularExpression grinderRe("\\*\\*Grinder\\*\\*:\\s*(.+?)\\n");
    QRegularExpressionMatch gm = grinderRe.match(content);
    QString grinder = gm.hasMatch() ? gm.captured(1).trimmed() : QString();

    // Detect anomaly flags
    bool channeling = content.contains("Channeling detected");
    bool tempUnstable = content.contains("Temperature unstable");

    // Build compact summary
    QString summary = "- Shot";
    if (!shotNum.isEmpty()) summary += " #" + shotNum;
    summary += ":";
    if (!profile.isEmpty()) summary += " \"" + profile + "\"";
    if (!dose.isEmpty() && !yield.isEmpty()) summary += " " + dose + "g\u2192" + yield + "g";
    if (!duration.isEmpty()) summary += ", " + duration + "s";
    if (!grinder.isEmpty()) {
        QString truncGrinder = grinder.length() > 30 ? grinder.left(27) + "..." : grinder;
        summary += ", " + truncGrinder;
    }
    if (!score.isEmpty()) summary += ", " + score + "/100";
    if (!notes.isEmpty()) {
        // Truncate long notes
        QString truncated = notes.length() > 40 ? notes.left(37) + "..." : notes;
        summary += ", \"" + truncated + "\"";
    }
    if (channeling) summary += " [channeling]";
    if (tempUnstable) summary += " [temp unstable]";

    return summary;
}

QString AIConversation::summarizeAdvice(const QString& response)
{
    // Extract the first actionable sentence from the AI's response.
    // Look for common recommendation patterns.
    // We take the first sentence that contains an action verb related to espresso dialing.

    // Try to find a line that starts with a recommendation keyword
    QRegularExpression recRe("(?:^|\\n)\\s*(?:[-•*]\\s*)?(?:Try|Adjust|Grind|Increase|Decrease|Lower|Raise|Change|Move|Use|Reduce|Extend|Shorten)\\s[^\\n]{5,}",
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
