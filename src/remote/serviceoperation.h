#pragma once

#include <QObject>
#include <QSet>
#include <QVariantMap>

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
