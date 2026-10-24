#include "remoteaccessibilitymanager.h"

#include "remotesessionbackend.h"
#include "../core/accessibilitytext.h"

#include <QAccessible>
#include <QGuiApplication>
#include <QJSEngine>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QWindow>

#include <type_traits>

static_assert(!std::is_default_constructible_v<RemoteAccessibilityManager>,
              "RemoteAccessibilityManager must only be created by its QML engine factory");

RemoteAccessibilityManager::RemoteAccessibilityManager(QObject* parent)
    : QObject(parent)
{
}

RemoteAccessibilityManager* RemoteAccessibilityManager::create(QQmlEngine* engine, QJSEngine*)
{
    auto* manager = new RemoteAccessibilityManager(engine);
    QJSEngine::setObjectOwnership(manager, QJSEngine::CppOwnership);
    return manager;
}

void RemoteAccessibilityManager::setSession(RemoteSessionBackend* session)
{
    if (m_session == session)
        return;
    if (m_snapshotConnection)
        disconnect(m_snapshotConnection);
    if (m_connectedConnection)
        disconnect(m_connectedConnection);
    if (m_staleConnection)
        disconnect(m_staleConnection);
    if (m_destroyedConnection)
        disconnect(m_destroyedConnection);

    m_session = session;
    if (m_session) {
        m_snapshotConnection = connect(m_session, &RemoteSessionBackend::snapshotChanged,
                                       this, [this] {
            refreshEnabled();
            refreshAnnouncementSettings();
        });
        m_connectedConnection = connect(m_session, &RemoteSessionBackend::connectedChanged,
                                        this, [this] {
            refreshEnabled();
            refreshAnnouncementSettings();
        });
        m_staleConnection = connect(m_session, &RemoteSessionBackend::staleChanged,
                                    this, [this] {
            refreshEnabled();
            refreshAnnouncementSettings();
        });
        m_destroyedConnection = connect(m_session, &QObject::destroyed, this, [this] {
            m_session = nullptr;
            m_snapshotConnection = {};
            m_connectedConnection = {};
            m_staleConnection = {};
            m_destroyedConnection = {};
            refreshEnabled();
            refreshAnnouncementSettings();
            emit sessionChanged();
        });
    }
    refreshEnabled();
    refreshAnnouncementSettings();
    emit sessionChanged();
}

bool RemoteAccessibilityManager::enabled() const
{
    return m_enabled;
}

void RemoteAccessibilityManager::refreshEnabled()
{
    const bool enabled = m_session && m_session->connected() && !m_session->stale()
                         && m_session->accessibilityEnabled();
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    emit enabledChanged();
}

void RemoteAccessibilityManager::refreshAnnouncementSettings()
{
    const bool fresh = m_session && m_session->connected() && !m_session->stale();
    const bool enabled = fresh ? m_session->extractionAnnouncementsEnabled() : true;
    const int interval = fresh ? m_session->extractionAnnouncementInterval() : 5;
    const QString mode = fresh ? m_session->extractionAnnouncementMode() : QStringLiteral("both");
    if (m_extractionAnnouncementsEnabled == enabled
        && m_extractionAnnouncementInterval == interval
        && m_extractionAnnouncementMode == mode)
        return;
    m_extractionAnnouncementsEnabled = enabled;
    m_extractionAnnouncementInterval = interval;
    m_extractionAnnouncementMode = mode;
    emit announcementSettingsChanged();
}

void RemoteAccessibilityManager::setLastAnnouncedItem(QObject* item)
{
    if (m_lastAnnouncedItem == item)
        return;
    if (m_itemDestroyedConnection)
        disconnect(m_itemDestroyedConnection);
    m_lastAnnouncedItem = item;
    if (m_lastAnnouncedItem) {
        m_itemDestroyedConnection = connect(m_lastAnnouncedItem, &QObject::destroyed, this, [this] {
            m_lastAnnouncedItem = nullptr;
            m_itemDestroyedConnection = {};
            emit lastAnnouncedItemChanged();
        });
    } else {
        m_itemDestroyedConnection = {};
    }
    emit lastAnnouncedItemChanged();
}

void RemoteAccessibilityManager::announce(const QString& text, bool interrupt)
{
    if (!enabled() || text.trimmed().isEmpty())
        return;

#ifndef QT_NO_ACCESSIBILITY
    QQuickWindow* target = qobject_cast<QQuickWindow*>(QGuiApplication::focusWindow());
    if (!target) {
        for (QWindow* window : QGuiApplication::topLevelWindows()) {
            target = qobject_cast<QQuickWindow*>(window);
            if (target)
                break;
        }
    }
    if (!target)
        return;

    QAccessibleAnnouncementEvent event(target, text);
    event.setPoliteness(interrupt ? QAccessible::AnnouncementPoliteness::Assertive
                                  : QAccessible::AnnouncementPoliteness::Polite);
    QAccessible::updateAccessibility(&event);
#else
    Q_UNUSED(interrupt);
#endif
}

void RemoteAccessibilityManager::announceLabel(const QString& text)
{
    announce(text, false);
}

QString RemoteAccessibilityManager::cleanForSpeech(const QString& text) const
{
    return AccessibilityText::cleanForSpeech(text);
}
