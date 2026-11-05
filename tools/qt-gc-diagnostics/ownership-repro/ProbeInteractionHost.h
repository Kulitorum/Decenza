#pragma once

#include <QObject>
#include <QPointer>
#include <QJSValue>
#include <QtQml/qqmlregistration.h>

// Only the native adapter's pointer-storage contract is reproduced here;
// no audio engine, settings, or platform accessibility service is started.
class ProbeInteractionHost : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool enabled MEMBER m_enabled CONSTANT)
    Q_PROPERTY(QJSValue retained READ retainedValue WRITE setRetainedValue)
    Q_PROPERTY(QObject *lastAnnouncedItem READ lastAnnouncedItem WRITE setLastAnnouncedItem
               NOTIFY lastAnnouncedItemChanged)
public:
    explicit ProbeInteractionHost(QObject *parent = nullptr) : QObject(parent) {}
    QJSValue retained;
    QJSValue retainedValue() const { return retained; }
    void setRetainedValue(const QJSValue &value) { retained = value; }
    QObject *lastAnnouncedItem() const { return m_lastAnnouncedItem; }
    void setLastAnnouncedItem(QObject *item)
    {
        if (m_lastAnnouncedItem == item)
            return;
        m_lastAnnouncedItem = item;
        emit lastAnnouncedItemChanged();
    }
signals:
    void destructionRecorded();
    void lastAnnouncedItemChanged();
private:
    bool m_enabled = true;
    QPointer<QObject> m_lastAnnouncedItem;
};
