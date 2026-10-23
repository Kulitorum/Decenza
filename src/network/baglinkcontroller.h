#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QPair>
#include <QSet>
#include <QVariantMap>
#include <QVector>

class BeanBaseClient;
class CoffeeBagStorage;
class SettingsDye;
class VisualizerShotSync;

// Host-owned coordinator for the link recovery and verification that belongs
// to a stored bag. QML asks to inspect a bag; this class re-reads the row and
// only commits a result if its original blob is still current.
class BagLinkController final : public QObject
{
    Q_OBJECT
public:
    BagLinkController(CoffeeBagStorage* storage, BeanBaseClient* beanBase,
                      SettingsDye* dye, VisualizerShotSync* visualizer,
                      QObject* parent = nullptr);

    void inspectBag(qint64 bagId, const QString& requestId);
    bool refreshVisualizerBags() const;

signals:
    void inspectionFinished(const QString& requestId, bool success, const QString& reason);

private:
    enum class Outcome { Recover, Resolve, Archive, Dead };
    enum class WaiterAdmission { Added, Existing, Rejected };
    struct Waiter {
        qint64 bagId = 0;
        QString expectedBlob;
        QString expectedLink;
        QString beanBaseId;
        QString coffeeName;
        QString imageKey;
        QString requestKey;
        Outcome outcome = Outcome::Resolve;
        bool activeRequired = false;
    };

    WaiterAdmission addWaiter(QHash<QString, QVector<Waiter>>& table, const QString& key,
                              const Waiter& waiter);
    void finishWaiters(QHash<QString, QVector<Waiter>>& table, const QString& key,
                       const QString& value, Outcome outcome);
    void clearWaiters(QHash<QString, QVector<Waiter>>& table, const QString& key);
    void apply(const Waiter& waiter, const QString& link, Outcome outcome);
    QString inspectRow(qint64 bagId, const QVariantMap& bag);
    void inspectAgain(qint64 bagId);
    void reapplyIfStillCurrent(const Waiter& waiter, const QString& link, Outcome outcome);
    static QString requestKey(qint64 bagId, const QString& imageKey, const QString& url);

    static constexpr qsizetype kMaximumWaiters = 512;
    static constexpr qsizetype kMaximumWaitersPerKey = 64;
    static constexpr qsizetype kMaximumRecoveredLinks = 512;

    QPointer<CoffeeBagStorage> m_storage;
    QPointer<BeanBaseClient> m_beanBase;
    QPointer<SettingsDye> m_dye;
    QPointer<VisualizerShotSync> m_visualizer;
    QHash<QString, QVector<Waiter>> m_recoveryWaiters;
    QHash<QString, QVector<Waiter>> m_validationWaiters;
    QHash<QString, QVector<Waiter>> m_archiveWaiters;
    QHash<QString, QString> m_recoveredLinks;
    QSet<qint64> m_pendingWrites;
    qsizetype m_waiterCount = 0;
};
