#pragma once

#include "rep_screensaverservice_source.h"
#include "serviceoperation.h"

#include <QPointer>
#include <QHash>
#include <QSet>
#include <atomic>
#include <memory>

class ScreensaverVideoManager;
class QNetworkReply;
class NativeImportServiceSource;

class NativeScreensaverServiceSource final : public ScreensaverStateSimpleSource
{
    Q_OBJECT
public:
    explicit NativeScreensaverServiceSource(ScreensaverVideoManager* manager,
                                             QObject* parent = nullptr);

    ~NativeScreensaverServiceSource() override;
    void execute(QString requestId, QString operation, QString ticket) override;
    void importMedia(QString requestId, QString transferId, QString clientId) override;
    void cancelMediaImport(QString requestId, QString targetRequestId, QString clientId) override;
    void setImportService(NativeImportServiceSource* service);
    void openClient(const QString& clientId);
    void closeClient(const QString& clientId);

private:
    void publishState();
    void cancelActiveMediaImport();
    void finish(const QString& requestId, const QString& operation, bool success,
                const QVariantMap& result = {}, const QString& reason = {});
    void finishAsyncOperation(const QString& operation);
    void failAsyncOperations(const QString& reason);

    QPointer<ScreensaverVideoManager> m_manager;
    QPointer<NativeImportServiceSource> m_importService;
    QSet<QString> m_clients;
    QString m_mediaImportId;
    QString m_mediaImportClient;
    bool m_mediaWorkerInFlight = false;
    quint64 m_personalCatalogGeneration = 0;
    std::shared_ptr<std::atomic_bool> m_mediaCancelled;
    ServiceRequestIds m_requests;
    QMultiHash<QString, QString> m_asyncRequests;
    QHash<QString, QString> m_asyncErrors;
    QPointer<QNetworkReply> m_shotMapReply;
    QByteArray m_shotMapPayload;
};
