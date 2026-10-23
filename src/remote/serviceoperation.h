#pragma once

#include <QObject>
#include <QHash>
#include <QSet>
#include <QVariantMap>
#include <functional>
#include <utility>

// One outstanding mutation per facade; wire IDs are independent of UI tokens.
class ServiceOperation final : public QObject
{
    Q_OBJECT
public:
    explicit ServiceOperation(QObject* parent = nullptr) : QObject(parent) {}
    bool busy() const { return !m_wireId.isEmpty(); }
    QString error() const { return m_error; }
    static QString newId();
    QString begin(const QString& operation, qint64 id, const QString& uiId, bool ready);
    void finish(const QString& wireId, bool success, qint64 resultId,
                const QString& reason, const QVariantMap& result = {});
    void disconnectPending();
    void clearError();
signals:
    void busyChanged();
    void errorChanged();
    void completed(const QString& uiId, const QString& operation, qint64 originalId,
                   bool success, qint64 resultId, const QString& reason,
                   const QVariantMap& result);
private:
    void report(quint64 generation, const QString& uiId, const QString& operation, qint64 originalId,
                bool success, qint64 resultId, const QString& reason,
                const QVariantMap& result = {});
    quint64 m_generation = 0;
    QString m_wireId;
    QString m_uiId;
    QString m_operation;
    QString m_error;
    qint64 m_id = -1;
};

// Bounds concurrent requests at the source, including reads. A duplicate must
// not complete or cancel the original caller's outstanding request.
class ServiceRequestIds
{
public:
    enum class Start { Accepted, InvalidId, DuplicateId, LimitReached };
    Start begin(const QString& id);
    bool finish(const QString& id) { return m_pending.remove(id); }
    qsizetype pendingCount() const { return m_pending.size(); }
private:
    QSet<QString> m_pending;
};

// Concurrent facade requests; admission limits and domain results stay with the
// facade. Detach requests before notifying callers, including on disconnect.
class ServiceRequests
{
public:
    bool isEmpty() const { return m_pending.isEmpty(); }
    qsizetype size() const { return m_pending.size(); }
    bool contains(const QString& id) const { return m_pending.contains(id); }
    // The ticket keeps an already queued dispatch tied to this admission.
    quint64 insert(const QString& id, const QString& operation);
    bool finish(const QString& id, const QString& operation);
    template<class Changed, class Failed>
    void failAll(Changed changed, Failed failed)
    {
        if (m_pending.isEmpty()) return;
        const auto pending = std::exchange(m_pending, {});
        changed();
        for (auto it = pending.cbegin(); it != pending.cend(); ++it)
            failed(it.key(), it->operation);
    }
    void dispatch(QObject* context, const QString& id, quint64 ticket, std::function<bool()> ready,
                  std::function<void()> call, std::function<void()> failed);
private:
    struct Request { QString operation; quint64 ticket; };
    QHash<QString, Request> m_pending;
    quint64 m_ticket = 0;
};
