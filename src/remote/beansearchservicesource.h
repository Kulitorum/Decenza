#pragma once

#include "rep_beansearchservice_source.h"
#include "serviceoperation.h"

#include <QHash>
#include <QPointer>
#include <atomic>
#include <memory>

class CoffeeBagStorage;
class Settings;
class BeanSearchQuery;

class NativeBeanSearchServiceSource final : public BeanSearchStateSimpleSource
{
    Q_OBJECT
public:
    NativeBeanSearchServiceSource(CoffeeBagStorage* bagStorage, Settings* settings,
                                  QString databasePath, QObject* parent = nullptr);
    ~NativeBeanSearchServiceSource() override;

    void search(QString requestId, QString cancelRequestId,
                QString query, QString bagKind) override;
    void cancelSearch(QString requestId) override;

private:
    void startHistoryRead(BeanSearchQuery* query);
    void publish(BeanSearchQuery* query, const QString& error = QString());
    void cancel(const QString& requestId);
    bool finish(BeanSearchQuery* query);

    QPointer<CoffeeBagStorage> m_bagStorage;
    QPointer<Settings> m_settings;
    QString m_databasePath;
    ServiceRequestIds m_requests;
    QHash<QString, BeanSearchQuery*> m_queries;
    // Cancelled searches no longer occupy m_queries, but their SQLite read
    // still runs to completion. Keep those workers inside the same fixed cap.
    int m_activeHistoryReads = 0;
    std::shared_ptr<std::atomic<bool>> m_destroyed = std::make_shared<std::atomic<bool>>(false);
};
