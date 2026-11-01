#include "conversationsessionsource.h"
#include "conversationsessionrequest.h"
#include "servicepayload.h"
#include "servicevalue.h"
#include "opaquefilebundlestore.h"
#include "core/appsettings.h"

#include "ai/aiconversation.h"
#include "ai/aimanager.h"
#include "history/shothistorystorage.h"

#include <QUuid>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRunnable>
#include <QThreadPool>
#include <QTemporaryDir>
#include <QRegularExpression>
#include <utility>

namespace {
constexpr qsizetype MaxMessageBytes = 32 * 1024;
constexpr qsizetype MaxTranscriptBytes = 1024 * 1024;
constexpr qsizetype MaxStatePayloadBytes = 1536 * 1024;
}

ConversationSessionSource::ConversationSessionSource(AIManager* manager,
                                                   ShotHistoryStorage* shotHistory, QObject* parent)
    : ConversationSessionStateSimpleSource(parent), m_manager(manager), m_shotHistory(shotHistory)
{
    if (m_manager) {
        connect(m_manager, &AIManager::conversationContextReady,
                this, &ConversationSessionSource::contextReady);
        connect(m_manager, &AIManager::configurationChanged, this, [this] {
            for (auto it = m_sessions.cbegin(); it != m_sessions.cend(); ++it)
                publishState(it.key());
        });
    }
    if (m_shotHistory) {
        connect(m_shotHistory, &ShotHistoryStorage::shotRequestReady,
                this, &ConversationSessionSource::storedShotReady);
    }
}

void ConversationSessionSource::setReportUrl(const QString& url)
{
    if (m_reportUrl == url)
        return;
    m_reportUrl = url;
    const auto clients = m_sessions.keys();
    for (const QString& clientId : clients)
        publishState(clientId);
}

void ConversationSessionSource::openClient(const QString& clientId)
{
    if (ConversationSessionRequest::isCanonicalUuid(clientId))
        m_openClients.insert(clientId);
}

void ConversationSessionSource::bindNativeSession(const QString& clientId)
{
    if (clientId.isEmpty() || !m_manager || !m_manager->conversation())
        return;
    if (!m_nativeSessionId.isEmpty() && m_nativeSessionId != clientId)
        m_sessions.remove(m_nativeSessionId);
    m_nativeSessionId = clientId;
    Session& session = m_sessions[clientId];
    session.native = true;
    session.conversation = m_manager->conversation();
    attachConversation(clientId, session.conversation);
}

ConversationSessionSource::Session* ConversationSessionSource::ensureSession(const QString& clientId)
{
    if (clientId.isEmpty() || !m_manager
        || (clientId != m_nativeSessionId && !m_openClients.contains(clientId)))
        return nullptr;
    auto it = m_sessions.find(clientId);
    if (it != m_sessions.end() && it->conversation)
        return &it.value();
    if (clientId == m_nativeSessionId) {
        bindNativeSession(clientId);
        it = m_sessions.find(clientId);
        return it == m_sessions.end() ? nullptr : &it.value();
    }
    if (m_sessions.size() >= MaxRemoteSessions)
        return nullptr;

    Session session;
    session.conversation = new AIConversation(m_manager, this);
    session.conversation->setTranslationManager(m_manager->translationManager());
    it = m_sessions.insert(clientId, session);
    attachConversation(clientId, it->conversation);
    return &it.value();
}

void ConversationSessionSource::attachConversation(const QString& clientId,
                                                   AIConversation* conversation)
{
    if (!conversation || m_clientByConversation.contains(conversation))
        return;
    m_clientByConversation.insert(conversation, clientId);
    const auto sessionIt = m_sessions.find(clientId);
    if (sessionIt != m_sessions.end())
        sessionIt->conversationIdentity = conversation;
    connect(conversation, &AIConversation::busyChanged, this,
            [this, conversation] { conversationChanged(conversation); });
    connect(conversation, &AIConversation::historyChanged, this,
            [this, conversation] { conversationChanged(conversation); });
    connect(conversation, &AIConversation::savedConversationChanged, this,
            [this, conversation] { conversationChanged(conversation); });
    connect(conversation, &AIConversation::contextLabelChanged, this,
            [this, conversation] { conversationChanged(conversation); });
    connect(conversation, &AIConversation::providerChanged, this,
            [this, conversation] { conversationChanged(conversation); });
    connect(conversation, &AIConversation::errorOccurred, this,
            [this, conversation] { conversationChanged(conversation); });
    connect(conversation, &QObject::destroyed, this, [this, conversation] {
        const QString ownerClientId = m_clientByConversation.take(conversation);
        if (!ownerClientId.isEmpty()) {
            auto it = m_sessions.find(ownerClientId);
            // Qt clears QPointer guards before emitting QObject::destroyed
            // (qtbase/src/corelib/kernel/qobject.cpp:2467), so compare the
            // non-owning identity recorded when this conversation was attached.
            if (it != m_sessions.end() && it->conversationIdentity == conversation) {
                it->conversation = nullptr;
                it->conversationIdentity = nullptr;
                if (!it->native)
                    m_sessions.erase(it);
            }
        }
    });
}

QVariantMap ConversationSessionSource::stateFor(const Session& session) const
{
    QVariantMap state;
    const AIConversation* conversation = session.conversation;
    if (!conversation || !m_manager)
        return state;
    state.insert(QStringLiteral("busy"), conversation->isBusy());
    state.insert(QStringLiteral("hasHistory"), conversation->hasHistory());
    state.insert(QStringLiteral("hasSavedConversation"), conversation->hasSavedConversation());
    QString transcript = conversation->getConversationText();
    QString lastResponse = conversation->lastResponse();
    const bool transcriptAvailable = transcript.toUtf8().size() <= MaxTranscriptBytes;
    if (!transcriptAvailable) {
        transcript.clear();
        lastResponse.clear();
    }
    state.insert(QStringLiteral("lastResponse"), lastResponse);
    state.insert(QStringLiteral("providerName"), conversation->providerName());
    state.insert(QStringLiteral("messageCount"), conversation->messageCount());
    state.insert(QStringLiteral("errorMessage"), transcriptAvailable
        ? conversation->errorMessage()
        : tr("This conversation is too large to display."));
    state.insert(QStringLiteral("canRetry"), conversation->canRetry());
    state.insert(QStringLiteral("contextLabel"), conversation->contextLabel());
    state.insert(QStringLiteral("transcript"), transcript);
    state.insert(QStringLiteral("transcriptAvailable"), transcriptAvailable);
    state.insert(QStringLiteral("contextLoading"), session.contextLoading);
    state.insert(QStringLiteral("shotId"), session.shotId);
    state.insert(QStringLiteral("shotLabel"), session.shotLabel);
    state.insert(QStringLiteral("beverageType"), session.beverageType);
    state.insert(QStringLiteral("pendingShotSummary"), session.pendingShotSummary);
    state.insert(QStringLiteral("isMistakeShot"), session.isMistakeShot);
    state.insert(QStringLiteral("tasteIntakeEnabled"), session.tasteIntakeEnabled);
    state.insert(QStringLiteral("aiConfigured"), m_manager->isConfigured());
    state.insert(QStringLiteral("reportUrl"), m_reportUrl);
    if (!ServicePayload::fits(state, MaxStatePayloadBytes)) {
        state.insert(QStringLiteral("lastResponse"), QString());
        state.insert(QStringLiteral("transcript"), QString());
        state.insert(QStringLiteral("transcriptAvailable"), false);
        state.insert(QStringLiteral("contextLabel"), QString());
        state.insert(QStringLiteral("shotLabel"), QString());
        state.insert(QStringLiteral("pendingShotSummary"), QString());
        state.insert(QStringLiteral("errorMessage"), tr("This conversation is too large to display."));
    }
    return state;
}

void ConversationSessionSource::publishState(const QString& clientId)
{
    auto it = m_sessions.find(clientId);
    if (it == m_sessions.end()) return;
    const QVariantMap state = stateFor(it.value());
    if (it->hasPublishedState && it->publishedState == state) return;
    it->publishedState = state;
    it->hasPublishedState = true;
    emit stateChanged(clientId, state);
}

void ConversationSessionSource::finish(const QString& clientId, const QString& requestId,
                                       const QString& operation, bool success,
                                       const QVariantMap& result, const QString& reason)
{
    endRequest(clientId, requestId);
    emit operationCompleted(clientId, requestId, operation, success, result, reason);
    publishState(clientId);
}

bool ConversationSessionSource::beginRequest(const QString& clientId, const QString& requestId,
                                             const QString& operation)
{
    if (!ConversationSessionRequest::isCanonicalUuid(clientId)
        || !ConversationSessionRequest::isCanonicalUuid(requestId)
        || (clientId != m_nativeSessionId && !m_openClients.contains(clientId)))
        return false;

    auto it = m_pendingRequestIds.find(clientId);
    if (it != m_pendingRequestIds.end() && it->contains(requestId))
        return false;
    if (it != m_pendingRequestIds.end() && it->size() >= MaxPendingRequestsPerClient) {
        emit operationCompleted(clientId, requestId, operation, false, {},
                                QStringLiteral("busy"));
        return false;
    }
    m_pendingRequestIds[clientId].insert(requestId);
    return true;
}

void ConversationSessionSource::endRequest(const QString& clientId, const QString& requestId)
{
    auto it = m_pendingRequestIds.find(clientId);
    if (it == m_pendingRequestIds.end())
        return;
    it->remove(requestId);
    if (it->isEmpty())
        m_pendingRequestIds.erase(it);
}

void ConversationSessionSource::fail(const QString& clientId, const QString& requestId,
                                     const QString& operation, const QString& reason)
{
    finish(clientId, requestId, operation, false, {}, reason);
}

void ConversationSessionSource::requestState(QString clientId, QString requestId)
{
    if (!beginRequest(clientId, requestId, QStringLiteral("state")))
        return;
    Session* session = ensureSession(clientId);
    if (!session) {
        fail(clientId, requestId, QStringLiteral("state"), QStringLiteral("unavailable"));
        return;
    }
    const QVariantMap state = stateFor(*session);
    endRequest(clientId, requestId);
    emit stateReady(clientId, requestId, state);
}

void ConversationSessionSource::loadMostRecent(QString clientId, QString requestId)
{
    const QString operation = QStringLiteral("loadMostRecent");
    if (!beginRequest(clientId, requestId, operation))
        return;
    Session* session = ensureSession(clientId);
    if (!session || !session->conversation || !m_manager) {
        fail(clientId, requestId, operation, QStringLiteral("unavailable"));
        return;
    }
    if (session->conversation->isBusy()) {
        fail(clientId, requestId, operation, QStringLiteral("busy"));
        return;
    }
    if (session->contextLoading) {
        fail(clientId, requestId, operation, QStringLiteral("contextLoading"));
        return;
    }
    session->shot.clear();
    session->shotId = 0;
    session->shotLabel.clear();
    session->beverageType = QStringLiteral("espresso");
    session->pendingShotSummary.clear();
    session->isMistakeShot = false;
    session->tasteIntakeEnabled = false;
    session->contextConversation = nullptr;
    session->contextRequestId.clear();
    session->contextRequestOperation.clear();
    ++session->contextGeneration;
    forgetShotLoads(clientId);
    const QPointer<ConversationSessionSource> guard(this);
    const QPointer<AIConversation> conversation = session->conversation;
    conversation->setShotIdForCurrentTurn(0);
    const auto index = m_manager->conversationIndex();
    if (index.isEmpty()) {
        conversation->setStorageKey(QString());
        conversation->setContextLabel(QString());
        if (!guard || !conversation || m_sessions.value(clientId).conversation != conversation) return;
        conversation->resetInMemory();
    } else {
        const auto& recent = index.first();
        conversation->setStorageKey(recent.key);
        conversation->setContextLabel(recent.label());
        if (!guard || !conversation || m_sessions.value(clientId).conversation != conversation) return;
        conversation->loadFromStorage();
    }
    if (!guard) return;
    finish(clientId, requestId, operation, true);
}

void ConversationSessionSource::openShot(QString clientId, QString requestId,
                                         QVariantMap shot, qint64 shotId,
                                         QString shotLabel)
{
    if (!beginRequest(clientId, requestId, QStringLiteral("openShot")))
        return;
    if (shotId < 0) {
        fail(clientId, requestId, QStringLiteral("openShot"), QStringLiteral("invalidPayload"));
        return;
    }
    if (shotLabel.size() > 512
        || !ServicePayload::fits(shot, ConversationSessionRequest::MaxShotPayloadBytes)) {
        fail(clientId, requestId, QStringLiteral("openShot"), QStringLiteral("payloadTooLarge"));
        return;
    }
    Session* session = ensureSession(clientId);
    if (!session || !session->conversation) {
        fail(clientId, requestId, QStringLiteral("openShot"), QStringLiteral("unavailable"));
        return;
    }
    if (session->conversation->isBusy()) {
        fail(clientId, requestId, QStringLiteral("openShot"), QStringLiteral("busy"));
        return;
    }
    if (session->contextLoading) {
        fail(clientId, requestId, QStringLiteral("openShot"), QStringLiteral("contextLoading"));
        return;
    }

    const bool tasteIntakeEnabled = m_manager->tasteIntakeOnAsk();

    if (shotId > 0) {
        if (!m_shotHistory || !ConversationSessionRequest::validStoredShotOverrides(shot)) {
            fail(clientId, requestId, QStringLiteral("openShot"), QStringLiteral("invalidPayload"));
            return;
        }
        session->contextLoading = true;
        session->pendingShotSummary.clear();
        session->tasteIntakeEnabled = false;
        session->contextConversation = session->conversation;
        const quint64 generation = ++session->contextGeneration;
        session->contextRequestId = requestId;
        session->contextRequestOperation = QStringLiteral("openShot");
        const QString internalRequestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_pendingShotLoads.insert(internalRequestId, PendingShotLoad{
            clientId, requestId, session->conversation, generation, shotId,
            std::move(shotLabel), std::move(shot), tasteIntakeEnabled
        });
        publishState(clientId);
        m_shotHistory->requestShotForRequest(internalRequestId, shotId);
        return;
    }

    prepareShotConversation(clientId, requestId, *session, std::move(shot), shotId,
                            std::move(shotLabel), tasteIntakeEnabled);
}

void ConversationSessionSource::prepareShotConversation(const QString& clientId,
                                                        const QString& requestId,
                                                        Session& session, QVariantMap shot,
                                                        qint64 shotId, QString shotLabel,
                                                        bool tasteIntakeEnabled)
{
    QString beverageType = shot.value(QStringLiteral("beverageType")).toString();
    if (beverageType.isEmpty())
        beverageType = QStringLiteral("espresso");
    if (!m_manager->isSupportedBeverageType(beverageType)) {
        session.contextLoading = false;
        session.contextConversation = nullptr;
        session.contextRequestId.clear();
        session.contextRequestOperation.clear();
        fail(clientId, requestId, QStringLiteral("openShot"), QStringLiteral("unsupportedBeverage"));
        return;
    }

    session.shot = shot;
    session.shotId = shotId;
    session.shotLabel = std::move(shotLabel);
    session.beverageType = beverageType;
    session.isMistakeShot = m_manager->isMistakeShot(shot);
    session.pendingShotSummary = session.isMistakeShot
        ? QString() : m_manager->buildShotAnalysisProseForShot(shot);
    session.tasteIntakeEnabled = tasteIntakeEnabled && !session.isMistakeShot && shotId > 0
        && shot.value(QStringLiteral("tasteBalance")).toString().isEmpty()
        && shot.value(QStringLiteral("tasteBody")).toString().isEmpty()
        && shot.value(QStringLiteral("enjoyment0to100")).toInt() <= 0;
    session.contextLoading = shotId > 0;
    if (!session.contextLoading || session.contextConversation != session.conversation)
        ++session.contextGeneration;
    session.contextConversation = session.contextLoading ? session.conversation : nullptr;
    session.contextRequestId = requestId;
    session.contextRequestOperation = QStringLiteral("openShot");

    m_manager->switchConversationFor(session.conversation, shot);
    publishState(clientId);
    if (shotId > 0) {
        m_manager->requestRecentShotContextFor(session.conversation, shot, shotId);
    } else {
        session.contextLoading = false;
        finish(clientId, requestId, QStringLiteral("openShot"), true);
    }
}

void ConversationSessionSource::sendMessage(QString clientId, QString requestId, QString text)
{
    if (!beginRequest(clientId, requestId, QStringLiteral("sendMessage")))
        return;
    Session* session = ensureSession(clientId);
    if (!session || !session->conversation || !m_manager) {
        fail(clientId, requestId, QStringLiteral("sendMessage"), QStringLiteral("unavailable"));
        return;
    }
    if (text.trimmed().isEmpty()) {
        fail(clientId, requestId, QStringLiteral("sendMessage"), QStringLiteral("emptyMessage"));
        return;
    }
    if (text.toUtf8().size() > MaxMessageBytes) {
        fail(clientId, requestId, QStringLiteral("sendMessage"), QStringLiteral("payloadTooLarge"));
        return;
    }
    if (session->conversation->isBusy() || m_manager->isAnalyzing() || !m_manager->isConfigured()) {
        fail(clientId, requestId, QStringLiteral("sendMessage"),
             (session->conversation->isBusy() || m_manager->isAnalyzing())
                 ? QStringLiteral("busy") : QStringLiteral("notConfigured"));
        return;
    }

    QString message = text;
    if (!session->pendingShotSummary.isEmpty()) {
        message = m_manager->buildConversationUserPromptFor(session->conversation,
            session->shot, text, session->shotLabel);
    }
    if (session->shotId > 0)
        session->conversation->setShotIdForCurrentTurn(session->shotId);

    bool accepted = false;
    if (!session->conversation->hasHistory()) {
        if (session->shot.isEmpty()) {
            fail(clientId, requestId, QStringLiteral("sendMessage"), QStringLiteral("noPriorConversation"));
            return;
        }
        m_manager->switchConversationFor(session->conversation, session->shot);
        const QString prompt = session->conversation->multiShotSystemPrompt(
            session->beverageType.toLower(), session->shot.value(QStringLiteral("profileName")).toString());
        session->conversation->ask(prompt, message);
        accepted = session->conversation->isBusy();
    } else {
        accepted = session->conversation->followUp(message);
    }
    if (!accepted) {
        fail(clientId, requestId, QStringLiteral("sendMessage"), QStringLiteral("notAccepted"));
        return;
    }
    session->pendingShotSummary.clear();
    finish(clientId, requestId, QStringLiteral("sendMessage"), true);
}

void ConversationSessionSource::retry(QString clientId, QString requestId)
{
    if (!beginRequest(clientId, requestId, QStringLiteral("retry")))
        return;
    Session* session = ensureSession(clientId);
    if (!session || !session->conversation || !session->conversation->canRetry()) {
        fail(clientId, requestId, QStringLiteral("retry"), QStringLiteral("notRetryable"));
        return;
    }
    if (m_manager && m_manager->isAnalyzing()) {
        fail(clientId, requestId, QStringLiteral("retry"), QStringLiteral("busy"));
        return;
    }
    session->conversation->retry();
    finish(clientId, requestId, QStringLiteral("retry"), session->conversation->isBusy(),
           {}, session->conversation->isBusy() ? QString() : QStringLiteral("notAccepted"));
}

void ConversationSessionSource::clear(QString clientId, QString requestId)
{
    if (!beginRequest(clientId, requestId, QStringLiteral("clear")))
        return;
    Session* session = ensureSession(clientId);
    if (!session || !session->conversation || session->conversation->isBusy()
        || session->contextLoading) {
        fail(clientId, requestId, QStringLiteral("clear"), QStringLiteral("busy"));
        return;
    }
    m_manager->clearConversationFor(session->conversation);
    session->contextRequestId.clear();
    ++session->contextGeneration;
    if (session->shotId > 0) {
        session->contextLoading = true;
        session->contextConversation = session->conversation;
        session->contextRequestId = requestId;
        session->contextRequestOperation = QStringLiteral("clear");
        publishState(clientId);
        m_manager->requestRecentShotContextFor(session->conversation, session->shot, session->shotId);
    } else {
        session->contextConversation = nullptr;
        finish(clientId, requestId, QStringLiteral("clear"), true);
    }
}

void ConversationSessionSource::dismissShot(QString clientId, QString requestId)
{
    if (!beginRequest(clientId, requestId, QStringLiteral("dismissShot")))
        return;
    Session* session = ensureSession(clientId);
    if (!session) {
        fail(clientId, requestId, QStringLiteral("dismissShot"), QStringLiteral("unavailable"));
        return;
    }
    session->pendingShotSummary.clear();
    finish(clientId, requestId, QStringLiteral("dismissShot"), true);
}

void ConversationSessionSource::save(QString clientId, QString requestId)
{
    if (!beginRequest(clientId, requestId, QStringLiteral("save")))
        return;
    Session* session = ensureSession(clientId);
    if (!session || !session->conversation) {
        fail(clientId, requestId, QStringLiteral("save"), QStringLiteral("unavailable"));
        return;
    }
    session->conversation->saveToStorage();
    finish(clientId, requestId, QStringLiteral("save"), true);
}

void ConversationSessionSource::archive(QString clientId, QString requestId,
                                         QString operation, QVariantMap fields)
{
    if (!beginRequest(clientId, requestId, operation)) return;
    if (!m_manager) {
        fail(clientId, requestId, operation, QStringLiteral("unavailable"));
        return;
    }
    if (operation == QLatin1String("listArchive")) {
        QVariantList entries;
        for (const auto& entry : m_manager->conversationIndex())
            entries.append(QVariantMap{{QStringLiteral("key"), entry.key},
                {QStringLiteral("label"), entry.label()}, {QStringLiteral("timestamp"), entry.timestamp}});
        finish(clientId, requestId, operation, true, {{QStringLiteral("entries"), entries}});
        return;
    }
    if (operation == QLatin1String("readBundleChunk") || operation == QLatin1String("releaseBundle")) {
        auto files = m_archiveFiles.value(clientId);
        QString reason;
        QVariantMap result;
        if (!files) reason = QStringLiteral("invalidBundle");
        else if (operation == QLatin1String("readBundleChunk")) result = files->readChunk(fields, &reason);
        else if (files->release(fields, &reason)) delete m_archiveFiles.take(clientId).data();
        finish(clientId, requestId, operation, reason.isEmpty(), result, reason);
        return;
    }
    const QString key = fields.value(QStringLiteral("key")).toString();
    const auto entry = m_manager->conversationEntry(key);
    const bool exporting = operation == QLatin1String("exportArchive");
    const QString format = fields.value(QStringLiteral("format")).toString();
    if ((operation != QLatin1String("readArchive") && !exporting) || key.isEmpty() || entry.key != key
        || (exporting && format != QLatin1String("text") && format != QLatin1String("json"))) {
        fail(clientId, requestId, operation, QStringLiteral("invalidArguments"));
        return;
    }
    if (exporting && (m_preparingArchive.contains(clientId)
        || (m_archiveFiles.value(clientId) && m_archiveFiles.value(clientId)->hasBundle()))) {
        fail(clientId, requestId, operation, QStringLiteral("busy"));
        return;
    }
    if (exporting) {
        delete m_archiveFiles.take(clientId).data();
        m_preparingArchive.insert(clientId);
    }
    const QPointer<OpaqueFileBundleStore> files = exporting ? new OpaqueFileBundleStore(this) : nullptr;
    if (exporting) m_archiveFiles.insert(clientId, files);
    const QPointer<ConversationSessionSource> self(this);
    QObject* context = QCoreApplication::instance();
    const QString label = entry.label();
    QThreadPool::globalInstance()->start(QRunnable::create(
        [self, context, files, clientId, requestId, operation, key, format, label, exporting] {
        AppSettings settings;
        const QJsonArray rows = AIConversation::exportConversationsStatic(settings, key);
        const QJsonObject data = rows.isEmpty() ? QJsonObject() : rows.first().toObject();
        QString reason = data.isEmpty() ? QStringLiteral("notFound") : QString();
        QVariantMap result;
        const bool text = format == QLatin1String("text");
        const QString suffix = text ? QStringLiteral(".txt") : QStringLiteral(".json");
        const QString bundleId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        auto directory = exporting ? std::make_shared<QTemporaryDir>() : std::shared_ptr<QTemporaryDir>();
        QString safeLabel = label;
        safeLabel.replace(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N} _-]")), QStringLiteral("_"));
        safeLabel = safeLabel.simplified().left(70).replace(QLatin1Char(' '), QLatin1Char('_'));
        const QString displayName = QStringLiteral("ai-conversation-") + safeLabel + suffix;
        if (reason.isEmpty() && exporting) {
            const QByteArray payload = text ? AIConversation::archivedConversationText(data).toUtf8()
                : QJsonDocument(data).toJson(QJsonDocument::Indented);
            QFile output(directory->filePath(bundleId + suffix));
            if (!directory->isValid() || !output.open(QIODevice::WriteOnly | QIODevice::NewOnly)
                || output.write(payload) != payload.size() || !output.flush())
                reason = QStringLiteral("exportFailed");
        } else if (reason.isEmpty()) {
            const QByteArray transcript = data.value(QStringLiteral("messages")).toArray().isEmpty()
                ? QByteArray() : AIConversation::archivedConversationText(data, false).toUtf8();
            result = {{QStringLiteral("text"), QString::fromUtf8(transcript.left(MaxTranscriptBytes))},
                {QStringLiteral("truncated"), transcript.size() > MaxTranscriptBytes},
                {QStringLiteral("label"), label}, {QStringLiteral("key"), key}};
            if (!ServicePayload::fits(result, MaxStatePayloadBytes)) {
                result[QStringLiteral("text")] = QString();
                result[QStringLiteral("truncated")] = true;
            }
        }
        QMetaObject::invokeMethod(context,
            [self, files, directory, clientId, requestId, operation, bundleId, suffix, text, displayName,
             exporting, result, reason]() mutable {
            if (!self || !self->m_pendingRequestIds.value(clientId).contains(requestId)) return;
            if (exporting) {
                self->m_preparingArchive.remove(clientId);
                if (!files || self->m_archiveFiles.value(clientId) != files)
                    reason = QStringLiteral("cancelled");
                else if (reason.isEmpty()) {
                    result = files->adopt(directory, bundleId, bundleId + suffix,
                        displayName,
                        ServiceValue::MaximumExactInteger,
                        text ? QStringLiteral("text/plain") : QStringLiteral("application/json"),
                        text ? QStringLiteral("Text files (*.txt)") : QStringLiteral("JSON files (*.json)"));
                    if (result.isEmpty()) reason = QStringLiteral("exportFailed");
                }
                if (!reason.isEmpty()) delete self->m_archiveFiles.take(clientId).data();
            }
            self->finish(clientId, requestId, operation, reason.isEmpty(), result, reason);
        }, Qt::QueuedConnection);
    }));
}

void ConversationSessionSource::closeClient(QString clientId)
{
    if (!ConversationSessionRequest::isCanonicalUuid(clientId))
        return;
    m_openClients.remove(clientId);
    m_pendingRequestIds.remove(clientId);
    m_preparingArchive.remove(clientId);
    delete m_archiveFiles.take(clientId).data();
    forgetShotLoads(clientId);
    auto it = m_sessions.find(clientId);
    if (it == m_sessions.end() || it->native)
        return;
    it->closeWhenIdle = true;
    if (it->conversation && it->conversation->isBusy()) {
        return;
    }
    maybeCloseSession(clientId);
}

void ConversationSessionSource::contextReady(AIConversation* conversation)
{
    const QString clientId = m_clientByConversation.value(conversation);
    auto it = m_sessions.find(clientId);
    if (it == m_sessions.end() || it->conversation != conversation
        || it->contextConversation != conversation || !it->contextLoading)
        return;
    it->contextLoading = false;
    it->contextConversation = nullptr;
    const QString requestId = std::exchange(it->contextRequestId, {});
    const QString operation = std::exchange(it->contextRequestOperation, {});
    publishState(clientId);
    if (!requestId.isEmpty())
        finish(clientId, requestId, operation, true);
}

void ConversationSessionSource::storedShotReady(const QString& internalRequestId, qint64 shotId,
                                                const QVariantMap& shot, const QString& reason)
{
    auto pendingIt = m_pendingShotLoads.find(internalRequestId);
    if (pendingIt == m_pendingShotLoads.end())
        return;
    const PendingShotLoad pending = pendingIt.value();
    m_pendingShotLoads.erase(pendingIt);

    auto sessionIt = m_sessions.find(pending.clientId);
    if (sessionIt == m_sessions.end() || !pending.conversation
        || sessionIt->conversation != pending.conversation
        || sessionIt->contextConversation != pending.conversation
        || sessionIt->contextGeneration != pending.generation
        || sessionIt->contextRequestId != pending.requestId)
        return;

    if (!reason.isEmpty() || shotId != pending.shotId
        || shot.value(QStringLiteral("id")).toLongLong() != pending.shotId) {
        sessionIt->contextLoading = false;
        sessionIt->contextConversation = nullptr;
        sessionIt->contextRequestId.clear();
        sessionIt->contextRequestOperation.clear();
        fail(pending.clientId, pending.requestId, QStringLiteral("openShot"),
             reason.isEmpty() ? QStringLiteral("notFound") : reason);
        return;
    }

    QVariantMap canonicalShot = shot;
    for (auto it = pending.overrides.cbegin(); it != pending.overrides.cend(); ++it)
        canonicalShot.insert(it.key(), it.value());
    prepareShotConversation(pending.clientId, pending.requestId, *sessionIt,
        std::move(canonicalShot), pending.shotId, pending.shotLabel,
        pending.tasteIntakeEnabled);
}

void ConversationSessionSource::conversationChanged(AIConversation* conversation)
{
    const QString clientId = m_clientByConversation.value(conversation);
    if (clientId.isEmpty())
        return;
    publishState(clientId);
    maybeCloseSession(clientId);
}

void ConversationSessionSource::maybeCloseSession(const QString& clientId)
{
    auto it = m_sessions.find(clientId);
    if (it == m_sessions.end() || it->native || !it->closeWhenIdle
        || (it->conversation && it->conversation->isBusy()))
        return;
    AIConversation* conversation = it->conversation;
    forgetShotLoads(clientId);
    m_sessions.erase(it);
    if (conversation)
        conversation->deleteLater();
}

void ConversationSessionSource::forgetShotLoads(const QString& clientId)
{
    for (auto it = m_pendingShotLoads.begin(); it != m_pendingShotLoads.end();) {
        if (it->clientId == clientId)
            it = m_pendingShotLoads.erase(it);
        else
            ++it;
    }
}
