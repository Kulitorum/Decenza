#pragma once

#include "remotesessionbackend.h"

#include <QObject>
#include <QMetaObject>
#include <QPointer>
#include <QtQmlIntegration/qqmlintegration.h>
#include <type_traits>

class QQmlEngine;
class QJSEngine;

// Browser platform adapter for the shared controls' app-level tap-to-announce
// policy. Announcements go through Qt accessibility events; browser/OS speech
// synthesis remains owned by the assistive technology.
class RemoteAccessibilityManager final : public QObject
{
    Q_OBJECT
    QML_SINGLETON
    QML_NAMED_ELEMENT(AccessibilityManager)

    Q_PROPERTY(RemoteSessionBackend* session READ session WRITE setSession NOTIFY sessionChanged FINAL)
    Q_PROPERTY(bool enabled READ enabled NOTIFY enabledChanged FINAL)
    Q_PROPERTY(bool extractionAnnouncementsEnabled READ extractionAnnouncementsEnabled
               NOTIFY announcementSettingsChanged FINAL)
    Q_PROPERTY(int extractionAnnouncementInterval READ extractionAnnouncementInterval
               NOTIFY announcementSettingsChanged FINAL)
    Q_PROPERTY(QString extractionAnnouncementMode READ extractionAnnouncementMode
               NOTIFY announcementSettingsChanged FINAL)
    Q_PROPERTY(QObject* lastAnnouncedItem READ lastAnnouncedItem WRITE setLastAnnouncedItem
               NOTIFY lastAnnouncedItemChanged FINAL)

public:
    static RemoteAccessibilityManager* create(QQmlEngine* engine, QJSEngine* jsEngine);

    RemoteSessionBackend* session() const { return m_session.data(); }
    void setSession(RemoteSessionBackend* session);
    bool enabled() const;
    bool extractionAnnouncementsEnabled() const { return m_extractionAnnouncementsEnabled; }
    int extractionAnnouncementInterval() const { return m_extractionAnnouncementInterval; }
    QString extractionAnnouncementMode() const { return m_extractionAnnouncementMode; }
    QObject* lastAnnouncedItem() const { return m_lastAnnouncedItem.data(); }
    void setLastAnnouncedItem(QObject* item);

    Q_INVOKABLE void announce(const QString& text, bool interrupt = false);
    Q_INVOKABLE void announceLabel(const QString& text);
    Q_INVOKABLE QString cleanForSpeech(const QString& text) const;

signals:
    void sessionChanged();
    void enabledChanged();
    void announcementSettingsChanged();
    void lastAnnouncedItemChanged();

private:
    explicit RemoteAccessibilityManager(QObject* parent = nullptr);
    void refreshEnabled();
    void refreshAnnouncementSettings();

    QPointer<RemoteSessionBackend> m_session;
    QPointer<QObject> m_lastAnnouncedItem;
    QMetaObject::Connection m_snapshotConnection;
    QMetaObject::Connection m_connectedConnection;
    QMetaObject::Connection m_staleConnection;
    QMetaObject::Connection m_destroyedConnection;
    QMetaObject::Connection m_itemDestroyedConnection;
    bool m_enabled = false;
    bool m_extractionAnnouncementsEnabled = true;
    int m_extractionAnnouncementInterval = 5;
    QString m_extractionAnnouncementMode = QStringLiteral("both");
};

static_assert(!std::is_default_constructible_v<RemoteAccessibilityManager>,
              "RemoteAccessibilityManager is a QML singleton with an engine factory; Qt must not "
              "construct an independent platform adapter.");
