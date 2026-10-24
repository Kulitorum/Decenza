#pragma once

#include "rep_screensaverservice_source.h"
#include "serviceoperation.h"

#include <QPointer>
#include <QHash>

class ScreensaverVideoManager;
class QNetworkReply;

class NativeScreensaverServiceSource final : public ScreensaverStateSimpleSource
{
    Q_OBJECT
public:
    explicit NativeScreensaverServiceSource(ScreensaverVideoManager* manager,
                                             QObject* parent = nullptr);

    void execute(QString requestId, QString operation, QString ticket) override;

private:
    void publishState();
    void finish(const QString& requestId, const QString& operation, bool success,
                const QVariantMap& result = {}, const QString& reason = {});
    void finishAsyncOperation(const QString& operation);
    void failAsyncOperations(const QString& reason);

    QPointer<ScreensaverVideoManager> m_manager;
    ServiceRequestIds m_requests;
    QMultiHash<QString, QString> m_asyncRequests;
    QHash<QString, QString> m_asyncErrors;
    QPointer<QNetworkReply> m_shotMapReply;
    QByteArray m_shotMapPayload;
};
