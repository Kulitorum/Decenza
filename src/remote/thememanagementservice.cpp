#include "thememanagementservice.h"

#include "serviceoperation.h"
#include <QQmlEngine>
#include <QRemoteObjectNode>

namespace {
QPointer<QRemoteObjectNode> s_publishedNode;
}

ThemeManagementService::ThemeManagementService(QRemoteObjectNode* node, QObject* parent)
    : RemoteService(parent)
{
    setNode(node);
}

ThemeManagementService::~ThemeManagementService()
{
    if (m_state) disconnect(m_state, nullptr, this, nullptr);
}

void ThemeManagementService::setPublishedNode(QRemoteObjectNode* node) { s_publishedNode = node; }
QRemoteObjectNode* ThemeManagementService::publishedNode() { return s_publishedNode; }
ThemeManagementService* ThemeManagementService::create(QQmlEngine* engine)
{
    return new ThemeManagementService(publishedNode(), engine);
}

bool ThemeManagementService::connected() const
{
    return RemoteServiceReplica::isReady(m_node, m_state, s_publishedNode);
}

QVariantList ThemeManagementService::presets() const
{
    return connected() ? m_state->presets() : QVariantList();
}
QVariantMap ThemeManagementService::activeColors() const
{
    return connected() ? m_state->activeColors() : QVariantMap();
}
QVariantMap ThemeManagementService::palettes() const
{
    return connected() ? m_state->palettes() : QVariantMap();
}
QString ThemeManagementService::activeThemeName() const
{
    return connected() ? m_state->activeThemeName() : QString();
}
bool ThemeManagementService::darkMode() const
{
    return connected() && m_state->darkMode();
}
qint64 ThemeManagementService::revision() const
{
    return connected() ? m_state->revision() : 0;
}
QString ThemeManagementService::themeMode() const { return connected() ? m_state->themeMode() : QString(); }
QStringList ThemeManagementService::themeNames() const { return connected() ? m_state->themeNames() : QStringList(); }
QString ThemeManagementService::darkThemeName() const { return connected() ? m_state->darkThemeName() : QString(); }
QString ThemeManagementService::lightThemeName() const { return connected() ? m_state->lightThemeName() : QString(); }
bool ThemeManagementService::glassChrome() const { return connected() && m_state->glassChrome(); }
QString ThemeManagementService::backgroundPreset() const { return connected() ? m_state->backgroundPreset() : QString(); }
QString ThemeManagementService::backgroundSource() const { return connected() ? m_state->backgroundSource() : QStringLiteral("none"); }
QString ThemeManagementService::backgroundPattern() const { return connected() ? m_state->backgroundPattern() : QString(); }
bool ThemeManagementService::backgroundShotAdvanced() const { return connected() && m_state->backgroundShotAdvanced(); }
QVariantList ThemeManagementService::backgroundImages() const { return connected() ? m_state->backgroundImages() : QVariantList(); }
QString ThemeManagementService::backgroundImageId() const { return connected() ? m_state->backgroundImageId() : QString(); }
QVariantList ThemeManagementService::backgroundPresets() const { return connected() ? m_state->backgroundPresets() : QVariantList(); }
QVariantList ThemeManagementService::backgroundPatterns() const { return connected() ? m_state->backgroundPatterns() : QVariantList(); }
bool ThemeManagementService::backgroundImageCacheEnabled() const { return connected() && m_state->backgroundImageCacheEnabled(); }
bool ThemeManagementService::ready() const
{
    return connected() && m_state->ready();
}
QString ThemeManagementService::errorMessage() const
{
    if (!connected()) return tr("Theme service is disconnected.");
    return m_state->errorMessage();
}

void ThemeManagementService::setNode(QRemoteObjectNode* node)
{
    if (m_node == node && m_state && (node || !m_nodeDestroyed)) return;
    disconnect(m_nodeDestroyed);
    m_nodeDestroyed = {};
    failPending(QStringLiteral("disconnected"));
    if (m_state) {
        disconnect(m_state, nullptr, this, nullptr);
        m_state->deleteLater();
    }
    m_node = node;
    m_state = node ? node->acquire<ThemeManagementStateReplica>()
                   : new ThemeManagementStateReplica;
    m_state->setParent(this);
    if (node)
        m_nodeDestroyed = connect(node, &QObject::destroyed, this,
                                  [this] { setNode(nullptr); });
    connect(m_state, &QRemoteObjectReplica::stateChanged, this,
            [this] { changed(); });
    connect(m_state, &QRemoteObjectReplica::notified, this,
            [this] { changed(); });
    connect(m_state, &ThemeManagementStateReplica::presetsChanged, this,
            &ThemeManagementService::changed);
    connect(m_state, &ThemeManagementStateReplica::activeColorsChanged, this,
            &ThemeManagementService::changed);
    connect(m_state, &ThemeManagementStateReplica::palettesChanged, this,
            &ThemeManagementService::changed);
    connect(m_state, &ThemeManagementStateReplica::activeThemeNameChanged, this,
            &ThemeManagementService::changed);
    connect(m_state, &ThemeManagementStateReplica::darkModeChanged, this,
            &ThemeManagementService::changed);
    connect(m_state, &ThemeManagementStateReplica::revisionChanged, this,
            &ThemeManagementService::changed);
    connect(m_state, &ThemeManagementStateReplica::readyChanged, this,
            &ThemeManagementService::changed);
    connect(m_state, &ThemeManagementStateReplica::errorMessageChanged, this,
            &ThemeManagementService::changed);
    connect(m_state, &ThemeManagementStateReplica::operationCompleted, this,
            [this](const QString& id, const QString& operation, bool success, qint64 revision,
                   const QVariantMap& result, const QString& reason) {
        if (m_pending.value(id) != operation) return;
        m_pending.remove(id);
        emit busyChanged();
        emit operationCompleted(id, operation, success, revision, result, reason);
    }, Qt::QueuedConnection);
    changed();
}

void ThemeManagementService::changed()
{
    if (!connected()) failPending(QStringLiteral("disconnected"));
    emit stateChanged();
}

void ThemeManagementService::failPending(const QString& reason)
{
    if (m_pending.isEmpty()) return;
    const auto pending = m_pending;
    m_pending.clear();
    emit busyChanged();
    for (auto it = pending.cbegin(); it != pending.cend(); ++it)
        emit operationCompleted(it.key(), it.value(), false, revision(), {}, reason);
}

QString ThemeManagementService::newRequestId() const { return ServiceOperation::newId(); }

void ThemeManagementService::refresh()
{
    if (connected()) m_state->refresh();
}

QString ThemeManagementService::execute(const QString& operation, const QString& name,
                                        qint64 expectedRevision, const QString& requestId)
{
    const QString id = requestId.isEmpty() ? newRequestId() : requestId;
    if (id.isEmpty() || m_pending.contains(id)) return {};
    if (!connected()) {
        QMetaObject::invokeMethod(this, [this, id, operation] {
            emit operationCompleted(id, operation, false, revision(), {},
                                    QStringLiteral("disconnected"));
        }, Qt::QueuedConnection);
        return id;
    }
    if (m_pending.size() >= 32) {
        QMetaObject::invokeMethod(this, [this, id, operation] {
            emit operationCompleted(id, operation, false, revision(), {},
                                    QStringLiteral("tooManyRequests"));
        }, Qt::QueuedConnection);
        return id;
    }
    const bool wasBusy = busy();
    m_pending.insert(id, operation);
    if (!wasBusy) emit busyChanged();
    m_state->execute(id, operation, name, expectedRevision);
    return id;
}

QString ThemeManagementService::setPaletteColor(const QString& palette, const QString& colorName,
                                                const QString& colorValue,
                                                qint64 expectedRevision,
                                                const QString& requestId)
{
    const QString id = requestId.isEmpty() ? newRequestId() : requestId;
    const QString operation = QStringLiteral("setPaletteColor");
    if (id.isEmpty() || m_pending.contains(id)) return {};
    if (!connected()) {
        QMetaObject::invokeMethod(this, [this, id, operation] {
            emit operationCompleted(id, operation, false, revision(), {},
                                    QStringLiteral("disconnected"));
        }, Qt::QueuedConnection);
        return id;
    }
    if (m_pending.size() >= 32) {
        QMetaObject::invokeMethod(this, [this, id, operation] {
            emit operationCompleted(id, operation, false, revision(), {},
                                    QStringLiteral("tooManyRequests"));
        }, Qt::QueuedConnection);
        return id;
    }
    const bool wasBusy = busy();
    m_pending.insert(id, operation);
    if (!wasBusy) emit busyChanged();
    m_state->setPaletteColor(id, palette, colorName, colorValue, expectedRevision);
    return id;
}

QString ThemeManagementService::executeSetting(const QString& operation,
                                               const QVariantMap& arguments,
                                               qint64 expectedRevision,
                                               const QString& requestId)
{
    const QString id = requestId.isEmpty() ? newRequestId() : requestId;
    if (id.isEmpty() || m_pending.contains(id)) return {};
    if (!connected()) {
        QMetaObject::invokeMethod(this, [this, id, operation] {
            emit operationCompleted(id, operation, false, revision(), {},
                                    QStringLiteral("disconnected"));
        }, Qt::QueuedConnection);
        return id;
    }
    if (m_pending.size() >= 32) {
        QMetaObject::invokeMethod(this, [this, id, operation] {
            emit operationCompleted(id, operation, false, revision(), {},
                                    QStringLiteral("tooManyRequests"));
        }, Qt::QueuedConnection);
        return id;
    }
    const bool wasBusy = busy();
    m_pending.insert(id, operation);
    if (!wasBusy) emit busyChanged();
    m_state->executeSetting(id, operation, arguments, expectedRevision);
    return id;
}

