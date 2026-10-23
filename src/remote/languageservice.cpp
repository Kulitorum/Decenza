#include "languageservice.h"

#include <QJSEngine>
#include <QMetaObject>
#include <QQmlEngine>
#include <QRemoteObjectNode>
#include <QUuid>

#include <utility>

namespace {
QPointer<QRemoteObjectNode> s_publishedNode;
}

LanguageService* LanguageService::create(QQmlEngine* engine, QJSEngine*)
{
    auto* service = new LanguageService(s_publishedNode, engine);
    QJSEngine::setObjectOwnership(service, QJSEngine::CppOwnership);
    return service;
}

LanguageService::LanguageService(QRemoteObjectNode* node, QObject* parent)
    : RemoteService(parent)
{
    setNode(node);
}

LanguageService::~LanguageService()
{
    if (m_state)
        disconnect(m_state, nullptr, this, nullptr);
}

void LanguageService::setPublishedNode(QRemoteObjectNode* node)
{
    s_publishedNode = node;
}

QRemoteObjectNode* LanguageService::publishedNode()
{
    return s_publishedNode;
}

void LanguageService::setNode(QRemoteObjectNode* node)
{
    if (m_node == node && m_state && (node || !m_nodeDestroyed))
        return;
    disconnect(m_nodeDestroyed);
    m_nodeDestroyed = {};
    failPending(QStringLiteral("disconnected"));
    m_lastReady = false;
    if (m_state) {
        auto* old = m_state.data();
        disconnect(old, nullptr, this, nullptr);
        old->deleteLater();
    }
    m_node = node;
    m_state = node ? node->acquire<LanguageStateReplica>() : new LanguageStateReplica;
    m_state->setParent(this);
    if (node)
        m_nodeDestroyed = connect(node, &QObject::destroyed, this, [this] { setNode(nullptr); });

    connect(m_state, &QRemoteObjectReplica::stateChanged,
            this, &LanguageService::observeConnectionState);
    connect(m_state, &QRemoteObjectReplica::notified, this, &LanguageService::stateChanged);
    connect(m_state, &LanguageStateReplica::snapshotChanged, this, [this] {
        const int version = snapshot().value(QStringLiteral("translationVersion")).toInt();
        const bool changed = m_lastStringVersion >= 0 && version != m_lastStringVersion;
        m_lastStringVersion = version;
        emit stateChanged();
        const QVariantMap context = currentContext();
        if (m_stringsRequested && (changed || context != m_requestedStringsContext))
            refreshStrings();
    });
    connect(m_state, &LanguageStateReplica::readyChanged,
            this, &LanguageService::observeConnectionState);
    connect(m_state, &LanguageStateReplica::operationCompleted,
            this, &LanguageService::finishPending, Qt::QueuedConnection);
    emit stateChanged();
}

bool LanguageService::connected() const
{
    return RemoteServiceReplica::isReady(m_node, m_state, publishedNode());
}

QVariantMap LanguageService::snapshot() const
{
    return connected() && m_state->ready() ? m_state->snapshot() : QVariantMap{};
}

bool LanguageService::ready() const
{
    return connected() && m_state->ready();
}

QStringList LanguageService::availableLanguages() const
{
    QStringList result;
    const QVariantList rows = snapshot().value(QStringLiteral("languages")).toList();
    result.reserve(rows.size());
    for (const QVariant& row : rows)
        result.append(row.toMap().value(QStringLiteral("code")).toString());
    return result;
}

bool LanguageService::downloading() const { return snapshot().value(QStringLiteral("downloading")).toBool(); }
bool LanguageService::uploading() const { return snapshot().value(QStringLiteral("uploading")).toBool(); }
bool LanguageService::scanning() const { return snapshot().value(QStringLiteral("scanning")).toBool(); }
int LanguageService::scanProgress() const { return snapshot().value(QStringLiteral("scanProgress")).toInt(); }
int LanguageService::scanTotal() const { return snapshot().value(QStringLiteral("scanTotal")).toInt(); }
bool LanguageService::autoTranslating() const { return snapshot().value(QStringLiteral("autoTranslating")).toBool(); }
int LanguageService::autoTranslateProgress() const { return snapshot().value(QStringLiteral("autoTranslateProgress")).toInt(); }
int LanguageService::autoTranslateTotal() const { return snapshot().value(QStringLiteral("autoTranslateTotal")).toInt(); }
QString LanguageService::retryStatus() const { return snapshot().value(QStringLiteral("retryStatus")).toString(); }
QString LanguageService::lastTranslatedText() const { return snapshot().value(QStringLiteral("lastTranslatedText")).toString(); }
int LanguageService::untranslatedCount() const { return snapshot().value(QStringLiteral("untranslatedCount")).toInt(); }
int LanguageService::totalStringCount() const { return snapshot().value(QStringLiteral("totalStringCount")).toInt(); }
int LanguageService::uniqueStringCount() const { return snapshot().value(QStringLiteral("uniqueStringCount")).toInt(); }
int LanguageService::uniqueUntranslatedCount() const { return snapshot().value(QStringLiteral("uniqueUntranslatedCount")).toInt(); }
bool LanguageService::canAutoTranslate() const { return snapshot().value(QStringLiteral("canAutoTranslate")).toBool(); }

QVariantMap languageRow(const QVariantMap& snapshot, const QString& code)
{
    const QVariantList rows = snapshot.value(QStringLiteral("languages")).toList();
    for (const QVariant& row : rows) {
        const QVariantMap map = row.toMap();
        if (map.value(QStringLiteral("code")).toString() == code)
            return map;
    }
    return {};
}

QString LanguageService::getLanguageDisplayName(const QString& code) const
{
    const QVariantMap row = languageRow(snapshot(), code);
    return row.value(QStringLiteral("displayName"), code).toString();
}

QString LanguageService::getLanguageNativeName(const QString& code) const
{
    const QVariantMap row = languageRow(snapshot(), code);
    return row.value(QStringLiteral("nativeName"), code).toString();
}

bool LanguageService::isRemoteLanguage(const QString& code) const
{
    return languageRow(snapshot(), code).value(QStringLiteral("isRemote")).toBool();
}

int LanguageService::getTranslationPercent(const QString& code) const
{
    return languageRow(snapshot(), code).value(QStringLiteral("percent")).toInt();
}

QString LanguageService::newRequestId() const
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
}

void LanguageService::refresh()
{
    if (connected())
        m_state->refresh();
}

void LanguageService::scanAllStrings() { execute(QStringLiteral("scanStrings")); }
void LanguageService::downloadLanguageList() { execute(QStringLiteral("refreshLanguageList")); }
void LanguageService::selectLanguage(const QString& code) { execute(QStringLiteral("selectLanguage"), {{QStringLiteral("code"), code}}); }
void LanguageService::downloadLanguage(const QString& code) { execute(QStringLiteral("downloadLanguage"), {{QStringLiteral("code"), code}}); }
void LanguageService::addLanguage(const QString& code, const QString& displayName, const QString& nativeName)
{
    execute(QStringLiteral("addLanguage"), {{QStringLiteral("code"), code},
        {QStringLiteral("displayName"), displayName}, {QStringLiteral("nativeName"), nativeName}});
}
void LanguageService::deleteLanguage(const QString& code) { execute(QStringLiteral("deleteLanguage"), {{QStringLiteral("code"), code}}); }
void LanguageService::submitTranslation() { execute(QStringLiteral("submitTranslation")); }
void LanguageService::autoTranslate() { execute(QStringLiteral("autoTranslate"), currentContext()); }
void LanguageService::cancelAutoTranslate() { execute(QStringLiteral("cancelAutoTranslate")); }

QString LanguageService::requestCatalogPage(qint64 revision, int offset)
{
    return execute(QStringLiteral("requestCatalogPage"),
                   {{QStringLiteral("revision"), revision}, {QStringLiteral("offset"), offset}});
}

QVariantMap LanguageService::currentContext() const
{
    const QVariantMap state = snapshot();
    return {{QStringLiteral("sourceId"), state.value(QStringLiteral("sourceId"))},
            {QStringLiteral("language"), state.value(QStringLiteral("currentLanguage"))},
            {QStringLiteral("revision"), state.value(QStringLiteral("catalogRevision"))}};
}

void LanguageService::refreshStrings()
{
    ++m_stringsGeneration;
    m_loadingStrings.clear();
    m_requestedStringsContext = currentContext();
    m_stringsTotal = 0;
    m_stringsRequested = true;
    m_stringsLoading = true;
    emit stringsChanged();
    requestStringPage(0);
}

void LanguageService::requestStringPage(int offset)
{
    QVariantMap arguments = m_requestedStringsContext;
    arguments.insert(QStringLiteral("offset"), offset);
    arguments.insert(QStringLiteral("limit"), 50);
    const QString requestId = execute(QStringLiteral("requestStrings"), arguments);
    const quint64 generation = m_stringsGeneration;
    m_internalRequestGenerations.insert(requestId, generation);
    // execute() queues a local refusal when the replica is unavailable or busy;
    // those requests never enter m_pending and therefore cannot complete through
    // finishPending(). Release the internal marker here and let a ready transition
    // retry a previously requested list.
    if (!m_pending.contains(requestId)) {
        m_internalRequestGenerations.remove(requestId);
        if (generation == m_stringsGeneration) {
            m_stringsLoading = false;
            emit stringsChanged();
        }
    }
}

void LanguageService::observeConnectionState()
{
    const bool nowReady = ready();
    if (!nowReady)
        failPending(QStringLiteral("disconnected"));
    const bool becameReady = nowReady && !m_lastReady;
    m_lastReady = nowReady;
    emit stateChanged();
    if (becameReady && m_stringsRequested && !m_stringsLoading)
        refreshStrings();
}

QString LanguageService::execute(const QString& operation, const QVariantMap& arguments)
{
    const QString requestId = newRequestId();
    if (m_pending.size() >= 32 || m_pending.contains(requestId) || !ready()) {
        const QString reason = ready() ? QStringLiteral("busy") : QStringLiteral("unavailable");
        QMetaObject::invokeMethod(this, [this, requestId, operation, reason] {
            m_errorMessage = reason;
            emit stateChanged();
            emit operationCompleted(requestId, operation, false, {}, reason);
        }, Qt::QueuedConnection);
        return requestId;
    }
    m_pending.insert(requestId, operation);
    m_errorMessage.clear();
    emit busyChanged();
    emit stateChanged();
    // All results, including synchronous refusals, return through the replica
    // signal. QRemoteObject may deliver the source signal before execute() has
    // returned, so install the pending identity before invoking it.
    m_state->execute(requestId, operation, arguments);
    return requestId;
}

void LanguageService::finishPending(const QString& requestId, const QString& operation,
                                    bool success, const QVariantMap& result,
                                    const QString& reason)
{
    const auto internalIt = m_internalRequestGenerations.find(requestId);
    const bool internalRequest = internalIt != m_internalRequestGenerations.end();
    const quint64 requestGeneration = internalRequest ? internalIt.value() : 0;
    const auto it = m_pending.constFind(requestId);
    if (it == m_pending.cend() || *it != operation)
        return;
    m_pending.remove(requestId);
    if (internalRequest)
        m_internalRequestGenerations.erase(internalIt);
    if (internalRequest && requestGeneration != m_stringsGeneration) {
        emit busyChanged();
        emit stateChanged();
        return;
    }
    if (!success && reason != QLatin1String("cancelled"))
        m_errorMessage = reason;
    emit busyChanged();
    emit stateChanged();

    if (internalRequest) {
        if (!success) {
            m_stringsLoading = false;
            m_errorMessage = reason;
            emit stringsChanged();
            emit stateChanged();
            return;
        }
        const QVariantMap page = result;
        const QVariantList rows = page.value(QStringLiteral("rows")).toList();
        if (page.value(QStringLiteral("sourceId")) != m_requestedStringsContext.value(QStringLiteral("sourceId"))
            || page.value(QStringLiteral("language")) != m_requestedStringsContext.value(QStringLiteral("language"))
            || page.value(QStringLiteral("revision")) != m_requestedStringsContext.value(QStringLiteral("revision"))
            || page.value(QStringLiteral("offset")).toInt() != m_loadingStrings.size()
            || (rows.isEmpty() && page.value(QStringLiteral("total")).toInt() > m_loadingStrings.size())) {
            m_stringsLoading = false;
            m_errorMessage = QStringLiteral("staleContext");
            emit stringsChanged();
            emit stateChanged();
            return;
        }
        m_loadingStrings.append(rows);
        m_stringsTotal = page.value(QStringLiteral("total")).toInt();
        if (m_loadingStrings.size() < m_stringsTotal) {
            requestStringPage(static_cast<int>(m_loadingStrings.size()));
        } else {
            m_strings = std::exchange(m_loadingStrings, {});
            m_stringsContext = m_requestedStringsContext;
            m_stringsLoading = false;
            emit stringsChanged();
        }
        return;
    }

    emit operationCompleted(requestId, operation, success, result, reason);
}

void LanguageService::failPending(const QString& reason)
{
    const auto pending = std::exchange(m_pending, {});
    const auto internalRequests = std::exchange(m_internalRequestGenerations, {});
    const bool hadInternalRequests = !internalRequests.isEmpty();
    const bool wasLoading = m_stringsLoading;
    if (pending.isEmpty() && !hadInternalRequests && !wasLoading)
        return;
    m_errorMessage = reason;
    if (hadInternalRequests || wasLoading)
        m_stringsLoading = false;
    emit busyChanged();
    emit stateChanged();
    if (hadInternalRequests || wasLoading)
        emit stringsChanged();
    for (auto it = pending.cbegin(); it != pending.cend(); ++it) {
        const QString requestId = it.key();
        const QString operation = it.value();
        if (internalRequests.contains(requestId))
            continue;
        QMetaObject::invokeMethod(this, [this, requestId, operation, reason] {
            emit operationCompleted(requestId, operation, false, {}, reason);
        }, Qt::QueuedConnection);
    }
}
