#pragma once

#include "rep_systemlogservice_source.h"

#include <QPointer>
#include <QStringList>
#include <QSet>
#include <atomic>
#include <functional>
#include <utility>
#include "opaquefilebundlestore.h"
#include "serviceoperation.h"

class WebDebugLogger;

// Host-owned persisted log reader and one raw live log stream. Bounded history
// snapshots and ZIP preparation run on the global worker pool.
class NativeSystemLogServiceSource final : public SystemLogStateSimpleSource
{
    Q_OBJECT
public:
    explicit NativeSystemLogServiceSource(WebDebugLogger* logger,
                                          QObject* parent = nullptr);

    void requestSnapshot(QString requestId, QStringList markers, QString minLevel,
                         int maxLines) override;
    void requestBoundary(QString requestId) override;
    void execute(QString requestId, QString operation, QVariantMap arguments) override;
    void openClient(const QString& clientId);
    void closeClient(const QString& clientId);
    void setMemorySummaryProvider(std::function<QString()> provider) { m_memorySummary = std::move(provider); }

private:
    static bool validRequestId(const QString& requestId);
    static QString validateFilter(const QStringList& markers,
                                  const QString& minLevel, int maxLines);
    void onLineAppended(const QString& line, qint64 sequence);

    void finish(QString requestId, QString operation, bool success,
                const QVariantMap& result = {}, const QString& reason = {});
    void clearBundle();
    void cancelPreparation();
    void prepareDownload(const QString& requestId, const QString& clientId, const QString& format);
    void downloadPrepared(const QString& requestId, const QString& clientId, const QString& format,
                          const QString& bundleId, const std::shared_ptr<QTemporaryDir>& directory,
                          const QString& failure);

    QPointer<WebDebugLogger> m_logger;
    std::function<QString()> m_memorySummary;
    QSet<QString> m_clients;
    ServiceRequestIds m_requests;
    OpaqueFileBundleStore m_bundles;
    QString m_activeId;
    QString m_activeClient;
    bool m_workerInFlight = false;
    int m_snapshotWorkers = 0;
    std::shared_ptr<std::atomic_bool> m_cancelled;
    QString m_bundleId;
    QString m_bundleClient;
    QString m_bundleRequest;
};
