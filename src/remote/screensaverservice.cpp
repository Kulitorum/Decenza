#include "screensaverservice.h"

#include "rep_screensaverservice_replica.h"
#include "screensaver/screensavervideomanager.h"
#include "core/memorymonitor.h"
#include "serviceoperation.h"
#include "servicevalue.h"

#include <QMetaType>
#include <QRemoteObjectNode>
#include <QRegularExpression>
#include <QUrl>
#include <QUuid>
#include <utility>

namespace {
QPointer<QRemoteObjectNode> s_publishedNode;
QPointer<ScreensaverVideoManager> s_localManager;
QPointer<MemoryMonitor> s_localMemoryMonitor;
std::function<void()> s_localWakeMachineAndScale;
std::function<void()> s_localWakeScale;
constexpr qsizetype kMaximumPendingOperations = 16;

bool validTicket(const QString& ticket)
{
    static const QRegularExpression expression(
        QStringLiteral("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$"));
    return expression.match(ticket).hasMatch();
}

bool validMediaResult(const QVariantMap& result)
{
    const QVariant ticket = result.value(QStringLiteral("ticket"));
    const QVariant image = result.value(QStringLiteral("isImage"));
    const QVariant author = result.value(QStringLiteral("author"));
    const QVariant source = result.value(QStringLiteral("sourceUrl"));
    const QVariant date = result.value(QStringLiteral("mediaDate"));
    const QVariant duration = result.value(QStringLiteral("durationSeconds"));
    if (result.size() != 6 || ticket.metaType().id() != QMetaType::QString || !validTicket(ticket.toString())
        || image.metaType().id() != QMetaType::Bool
        || author.metaType().id() != QMetaType::QString || author.toString().size() > 256
        || source.metaType().id() != QMetaType::QString || source.toString().size() > 2048
        || date.metaType().id() != QMetaType::QString || date.toString().size() > 128)
        return false;
    int seconds = 0;
    return ServiceValue::boundedInteger(duration, 0, 86400, &seconds);
}

void queueFailures(ScreensaverService* service, const QHash<QString, QString>& pending,
                   const QString& reason)
{
    for (auto it = pending.cbegin(); it != pending.cend(); ++it) {
        const QString requestId = it.key();
        const QString operation = it.value();
        QMetaObject::invokeMethod(service, [service, requestId, operation, reason] {
            emit service->completed(requestId, operation, false, {}, reason);
        }, Qt::QueuedConnection);
    }
}
}

ScreensaverService::ScreensaverService(QRemoteObjectNode* node, QObject* parent)
    : RemoteService(parent)
{
    setNode(node);
}

ScreensaverService::~ScreensaverService()
{
    if (!m_state) return;
    disconnect(m_state, nullptr, this, nullptr);
    const QString clientId = m_node ? m_node->property("decenzaClientId").toString() : QString();
    if (!ready() || clientId.isEmpty()) return;
    for (auto it = m_pending.cbegin(); it != m_pending.cend(); ++it)
        if (it.value() == QLatin1String("import-media"))
            m_state->cancelMediaImport(newRequestId(), it.key(), clientId);
}

void ScreensaverService::setPublishedNode(QRemoteObjectNode* node) { s_publishedNode = node; }
void ScreensaverService::setLocalManager(ScreensaverVideoManager* manager) { s_localManager = manager; }
void ScreensaverService::setLocalMemoryMonitor(MemoryMonitor* monitor) { s_localMemoryMonitor = monitor; }
void ScreensaverService::setLocalWakeCallbacks(std::function<void()> wakeMachineAndScale,
                                               std::function<void()> wakeScale)
{
    s_localWakeMachineAndScale = std::move(wakeMachineAndScale);
    s_localWakeScale = std::move(wakeScale);
}
QRemoteObjectNode* ScreensaverService::publishedNode() { return s_publishedNode; }
ScreensaverVideoManager* ScreensaverService::localManager() { return s_localManager; }

bool ScreensaverService::instrumentedBuild() const
{
#ifdef __EMSCRIPTEN__
    return false;
#else
    return s_localMemoryMonitor && s_localMemoryMonitor->instrumentedBuild();
#endif
}

double ScreensaverService::liveRssMB() const
{
#ifdef __EMSCRIPTEN__
    return -1.0;
#else
    return s_localMemoryMonitor ? s_localMemoryMonitor->liveRssMB() : -1.0;
#endif
}

void ScreensaverService::wakeMachineAndScale()
{
#ifndef __EMSCRIPTEN__
    if (s_localWakeMachineAndScale)
        s_localWakeMachineAndScale();
#endif
}

void ScreensaverService::wakeScale()
{
#ifndef __EMSCRIPTEN__
    if (s_localWakeScale)
        s_localWakeScale();
#endif
}

bool ScreensaverService::ready() const
{
    return RemoteServiceReplica::isReady(m_node, m_state, s_publishedNode)
        && m_state->ready();
}

bool ScreensaverService::isRemoteClient() const
{
#ifdef __EMSCRIPTEN__
    return true;
#else
    return false;
#endif
}

bool ScreensaverService::hasHardwareVideoDecoder() const
{
#ifdef __EMSCRIPTEN__
    // Browser decoder availability is determined by the per-client MediaPlayer
    // attempt; it must not inherit the host device's codec capabilities.
    return true;
#else
    return s_localManager && s_localManager->hasHardwareVideoDecoder();
#endif
}

QString ScreensaverService::newRequestId() const
{
    return ServiceOperation::newId();
}

QString ScreensaverService::nextMedia()
{
    return execute(QStringLiteral("next-media"));
}

QString ScreensaverService::setScreensaverType(const QString& type)
{
    return execute(QStringLiteral("set-type"), type);
}

QString ScreensaverService::refreshCategories()
{
    return execute(QStringLiteral("refresh-categories"));
}

QString ScreensaverService::refreshCatalog()
{
    return execute(QStringLiteral("refresh-catalog"));
}

QString ScreensaverService::clearVideoCache()
{
    return execute(QStringLiteral("clear-cache"));
}

QString ScreensaverService::clearPersonalMedia()
{
    return execute(QStringLiteral("clear-personal"));
}

QString ScreensaverService::refreshShotMap()
{
    return execute(QStringLiteral("refresh-shot-map"));
}

QString ScreensaverService::importMedia(const QString& transferId)
{
    return execute(QStringLiteral("import-media"), transferId);
}

QString ScreensaverService::cancelMediaImport(const QString& requestId)
{
    return execute(QStringLiteral("cancel-media-import"), requestId);
}

QStringList ScreensaverService::overlayChipsForType(const QString& type) const
{
    if (type == QLatin1String("disabled"))
        return {};
    if (type == QLatin1String("flipclock"))
        return {QStringLiteral("waterLevel"), QStringLiteral("shotPlan"),
                QStringLiteral("battery"), QStringLiteral("linkButton")};
    if (type == QLatin1String("videos") || type == QLatin1String("pipes")
        || type == QLatin1String("attractor") || type == QLatin1String("shotmap"))
        return {QStringLiteral("clock"), QStringLiteral("waterLevel"),
                QStringLiteral("shotPlan"), QStringLiteral("battery"),
                QStringLiteral("linkButton")};
    return {};
}

QString ScreensaverService::markPlayed(const QString& ticket)
{
    return execute(QStringLiteral("mark-played"), ticket);
}

QString ScreensaverService::markCorrupt(const QString& ticket)
{
    return execute(QStringLiteral("mark-corrupt"), ticket);
}

QString ScreensaverService::mediaSource(const QString& ticket) const
{
    if (!validTicket(ticket))
        return {};
#ifdef __EMSCRIPTEN__
    QUrl origin = resourceOrigin();
    if (!origin.isValid() || origin.isEmpty())
        return {};
    origin.setPath(QStringLiteral("/api/screensaver/media/") + ticket);
    origin.setQuery(QString());
    origin.setFragment(QString());
    return origin.toString(QUrl::FullyEncoded);
#else
    ScreensaverMediaTicket media;
    if (!s_localManager || !s_localManager->resolveMediaTicket(ticket, &media))
        return {};
    return QUrl::fromLocalFile(media.path).toString();
#endif
}

QUrl ScreensaverService::backgroundImageUrl(const QString& opaqueImageId) const
{
    if (opaqueImageId.size() != 64) return {};
#ifdef __EMSCRIPTEN__
    QUrl url = resourceOrigin();
    url.setPath(QStringLiteral("/api/session/background-preview/") + opaqueImageId);
    return url;
#else
    if (!localManager()) return {};
    const QString path = localManager()->resolveBackgroundImageToken(opaqueImageId);
    return path.isEmpty() ? QUrl{} : QUrl::fromLocalFile(path);
#endif
}

void ScreensaverService::setScreenDimming(int dimPercent)
{
#ifndef __EMSCRIPTEN__
    if (s_localManager)
        s_localManager->setScreenDimming(qBound(0, dimPercent, 100));
#else
    Q_UNUSED(dimPercent);
#endif
}

void ScreensaverService::restoreScreenBrightness()
{
#ifndef __EMSCRIPTEN__
    if (s_localManager)
        s_localManager->restoreScreenBrightness();
#endif
}

void ScreensaverService::setKeepScreenOn(bool on)
{
#ifndef __EMSCRIPTEN__
    if (s_localManager)
        s_localManager->setKeepScreenOn(on);
#else
    Q_UNUSED(on);
#endif
}

void ScreensaverService::setNode(QRemoteObjectNode* node)
{
    const auto previous = m_state;
    const QString previousClient = m_node ? m_node->property("decenzaClientId").toString() : QString();
    if (!RemoteServiceReplica::replace(this, node, m_node, m_state, m_nodeDestroyed,
            [this, previous, previousClient] {
        const QPointer<ScreensaverService> guard(this);
        // Native calls can reenter here (Qt 6.12 qremoteobjectreplica.cpp:874-891).
        const auto pending = std::exchange(m_pending, {});
        queueFailures(this, pending, QStringLiteral("Screensaver connection lost; the operation was not retried."));
        if (previous && previous->isInitialized() && !previousClient.isEmpty()) {
            for (auto it = pending.cbegin(); it != pending.cend(); ++it) {
                if (!previous) break;
                if (it.value() == QLatin1String("import-media")) {
                    previous->cancelMediaImport(newRequestId(), it.key(), previousClient);
                    if (!guard) return;
                }
            }
        }
        if (!pending.isEmpty()) emit busyChanged();
    }))
        return;
    connect(m_state, &QRemoteObjectReplica::stateChanged, this, &ScreensaverService::onStateChanged);
    connect(m_state, &QRemoteObjectReplica::notified, this, &ScreensaverService::onStateChanged);
    connect(m_state, &ScreensaverStateReplica::readyChanged, this, &ScreensaverService::onStateChanged);
    connect(m_state, &ScreensaverStateReplica::operationCompleted, this,
            [this](const QString& requestId, const QString& operation, bool success,
                   const QVariantMap& result, const QString& reason) {
        if (!m_pending.contains(requestId))
            return;
        const QString expected = m_pending.value(requestId);
        if (expected != operation)
            return;
        if (success && operation == QStringLiteral("next-media") && !validMediaResult(result)) {
            finish(requestId, operation, false, {}, QStringLiteral("The host returned invalid media metadata."));
        } else {
            finish(requestId, operation, success, result, reason);
        }
    }, Qt::QueuedConnection);
    connect(m_state, &ScreensaverStateReplica::mediaAvailable,
            this, &ScreensaverService::mediaAvailable, Qt::QueuedConnection);
    emit stateChanged();
}

void ScreensaverService::onStateChanged()
{
    const QPointer<ScreensaverService> guard(this);
    if (!ready())
        failPending(QStringLiteral("Screensaver connection lost; the operation was not retried."));
    if (guard) emit stateChanged();
}

QString ScreensaverService::execute(const QString& operation, const QString& ticket)
{
    const QString requestId = newRequestId();
    if (!ready()) {
        QMetaObject::invokeMethod(this, [this, requestId, operation] {
            emit completed(requestId, operation, false, {}, QStringLiteral("Screensaver service is not ready."));
        }, Qt::QueuedConnection);
        return requestId;
    }
    if (m_pending.size() >= kMaximumPendingOperations) {
        QMetaObject::invokeMethod(this, [this, requestId, operation] {
            emit completed(requestId, operation, false, {}, QStringLiteral("Screensaver service is busy."));
        }, Qt::QueuedConnection);
        return requestId;
    }
    if ((operation == QStringLiteral("mark-played") || operation == QStringLiteral("mark-corrupt")
         || operation == QStringLiteral("import-media") || operation == QStringLiteral("cancel-media-import"))
        && !validTicket(ticket)) {
        QMetaObject::invokeMethod(this, [this, requestId, operation] {
            emit completed(requestId, operation, false, {}, QStringLiteral("Invalid media ticket."));
        }, Qt::QueuedConnection);
        return requestId;
    }
    if (operation == QStringLiteral("set-type") && ticket.isEmpty()) {
        QMetaObject::invokeMethod(this, [this, requestId, operation] {
            emit completed(requestId, operation, false, {}, QStringLiteral("Invalid screensaver type."));
        }, Qt::QueuedConnection);
        return requestId;
    }
    const QString clientId = m_node ? m_node->property("decenzaClientId").toString() : QString();
    if ((operation == QLatin1String("import-media") || operation == QLatin1String("cancel-media-import"))
        && clientId.isEmpty()) {
        QMetaObject::invokeMethod(this, [this, requestId, operation] {
            emit completed(requestId, operation, false, {}, QStringLiteral("clientUnavailable"));
        }, Qt::QueuedConnection);
        return requestId;
    }
    const QPointer<ScreensaverService> guard(this);
    const auto requestedState = m_state;
    m_pending.insert(requestId, operation);
    emit busyChanged();
    if (!guard || m_state != requestedState || !ready()
        || m_pending.value(requestId) != operation) return requestId;
    if (operation == QLatin1String("import-media")) requestedState->importMedia(requestId, ticket, clientId);
    else if (operation == QLatin1String("cancel-media-import")) requestedState->cancelMediaImport(requestId, ticket, clientId);
    else requestedState->execute(requestId, operation, ticket);
    return requestId;
}

void ScreensaverService::finish(const QString& requestId, const QString& operation, bool success,
                                const QVariantMap& result, const QString& reason)
{
    if (!m_pending.remove(requestId))
        return;
    const QPointer<ScreensaverService> guard(this);
    if (success) {
        if (!m_errorMessage.isEmpty()) {
            m_errorMessage.clear();
            emit stateChanged();
        }
    } else {
        m_errorMessage = reason;
        emit stateChanged();
    }
    if (!guard) return;
    emit busyChanged();
    if (!guard) return;
    emit completed(requestId, operation, success, result, reason);
}

void ScreensaverService::failPending(const QString& reason)
{
    const QPointer<ScreensaverService> guard(this);
    const auto pending = std::exchange(m_pending, {});
    if (pending.isEmpty())
        return;
    emit busyChanged();
    if (!guard) return;
    queueFailures(this, pending, reason);
}
