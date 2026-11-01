#include "remotemachineuisource.h"
#include <utility>
#include <QMetaProperty>
#include "core/uiworkstats.h"

RemoteMachineUiSource::RemoteMachineUiSource(QObject* parent) : QObject(parent) {}

void RemoteMachineUiSource::setSession(RemoteSessionBackend* session)
{
    if (m_session == session)
        return;
    for (const auto& connection : std::as_const(m_connections))
        disconnect(connection);
    m_connections.clear();
    m_session = session;
    ++m_generation;
    m_publishedValues.clear();
    if (session) {
        m_connections.append(connect(session, &RemoteSessionBackend::snapshotChanged,
                                     this, &RemoteMachineUiSource::publishChanges));
        m_connections.append(connect(session, &RemoteSessionBackend::connectedChanged,
                                     this, &RemoteMachineUiSource::publishChanges));
        m_connections.append(connect(session, &RemoteSessionBackend::staleChanged,
                                     this, &RemoteMachineUiSource::publishChanges));
        m_connections.append(connect(session, &QObject::destroyed, this, [this] {
            m_session = nullptr;
            ++m_generation;
            const QPointer<RemoteMachineUiSource> guard(this);
            emit sessionChanged();
            if (guard) publishChanges();
        }));
    }
    const QPointer<RemoteMachineUiSource> guard(this);
    const auto generation = m_generation;
    emit sessionChanged();
    if (guard && generation == m_generation) publishChanges();
}

void RemoteMachineUiSource::publishChanges()
{
    UiWorkStats::Span work(UiWorkStats::Kind::BrowserBindings);
    QList<QMetaMethod> notifications;
    const QMetaObject* meta = metaObject();
    for (int index = RemoteMachineUiSource::staticMetaObject.propertyCount();
         index < meta->propertyCount(); ++index) {
        const auto property = meta->property(index);
        if (!property.hasNotifySignal()
            || property.metaType().flags().testFlag(QMetaType::PointerToQObject)) continue;
        const auto signal = property.notifySignal();
        if (signal.name() != QByteArray(property.name()) + "Changed") continue;
        const QVariant value = property.read(this);
        const auto prior = m_publishedValues.constFind(index);
        if (prior == m_publishedValues.cend() || prior.value() != value)
            notifications.append(signal);
        m_publishedValues.insert(index, value);
        ++work.items;
    }
    work.changes = notifications.size();
    // Commit every value before notifying: a handler may replace this session.
    const QPointer<RemoteMachineUiSource> guard(this);
    const auto generation = m_generation;
    for (const auto& signal : notifications) {
        (void)signal.invoke(this, Qt::DirectConnection);
        if (!guard || generation != m_generation) return;
    }
    emit machineUiChanged();
}

bool RemoteMachineUiSource::sessionIsFresh() const
{
    return m_session && m_session->connected() && !m_session->stale();
}

const MachineUiSnapshot& RemoteMachineUiSource::machineUi() const
{
    static const MachineUiSnapshot disconnected;
    return sessionIsFresh()
        ? m_session->machineUi() : disconnected;
}
