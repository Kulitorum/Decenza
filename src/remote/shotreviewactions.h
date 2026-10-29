#pragma once

#include "remoteservice.h"
#include "rep_shotreviewactions_replica.h"

#include <QHash>
#include <QPointer>
#include <QtQmlIntegration/qqmlintegration.h>

class QRemoteObjectNode;
class QRemoteObjectPendingCallWatcher;
class BLEManager;
class RefractometerProxy;

class ShotReviewActions final : public RemoteService
{
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString clientId READ clientId NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool visualizerUploading READ visualizerUploading NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool decentUploading READ decentUploading NOTIFY stateChanged FINAL)
    Q_PROPERTY(qint64 decentLastShotId READ decentLastShotId NOTIFY stateChanged FINAL)
    Q_PROPERTY(int decentLastHttpStatus READ decentLastHttpStatus NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString decentLastSerial READ decentLastSerial NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString decentResultCode READ decentResultCode NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString visualizerLastUploadStatus READ visualizerLastUploadStatus NOTIFY stateChanged FINAL)
    Q_PROPERTY(int decentUploadedResult READ decentUploadedResult NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool visualizerActive READ visualizerActive NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool decentActive READ decentActive NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool aiConfigured READ aiConfigured NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool aiAnalyzing READ aiAnalyzing NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString discussShotUrl READ discussShotUrl NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool discussReady READ discussReady NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool discussEnabled READ discussEnabled NOTIFY stateChanged FINAL)
    // The Discuss app reads the shot through MCP; otherwise it is put on the clipboard.
    Q_PROPERTY(bool discussAppReadsShot READ discussAppReadsShot NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool refractometerConfigured READ refractometerConfigured NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool refractometerConnected READ refractometerConnected NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool refractometerMeasuring READ refractometerMeasuring NOTIFY stateChanged FINAL)
    Q_PROPERTY(double refractometerTds READ refractometerTds NOTIFY stateChanged FINAL)
    Q_PROPERTY(double refractometerTemperature READ refractometerTemperature NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString refractometerName READ refractometerName NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString savedRefractometerAddress READ savedRefractometerAddress NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString savedRefractometerName READ savedRefractometerName NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString refractometerOwnerReviewId READ refractometerOwnerReviewId NOTIFY stateChanged FINAL)
public:
    ~ShotReviewActions() override;
    static void setPublishedNode(QRemoteObjectNode* node);
    void setNode(QRemoteObjectNode* node) override;
    bool ready() const;
    QString clientId() const { return m_node ? m_clientId : QString(); }
    bool visualizerUploading() const;
    bool decentUploading() const;
    qint64 decentLastShotId() const;
    int decentLastHttpStatus() const;
    QString decentLastSerial() const;
    QString decentResultCode() const;
    QString visualizerLastUploadStatus() const;
    int decentUploadedResult() const;
    bool visualizerActive() const;
    bool decentActive() const;
    bool aiConfigured() const;
    bool aiAnalyzing() const;
    QString discussShotUrl() const;
    bool discussReady() const;
    bool discussEnabled() const;
    bool discussAppReadsShot() const;
    bool refractometerConfigured() const;
    bool refractometerConnected() const;
    bool refractometerMeasuring() const;
    double refractometerTds() const;
    double refractometerTemperature() const;
    QString refractometerName() const;
    QString savedRefractometerAddress() const;
    QString savedRefractometerName() const;
    QString refractometerOwnerReviewId() const;
    Q_INVOKABLE QString newRequestId() const;
    Q_INVOKABLE QString executeFileOperation(const QString& operation, const QVariantMap& arguments);
    Q_INVOKABLE QString holdShotEdits(qint64 shotId, const QString& reviewId);
    Q_INVOKABLE void expectHeldShotEdit(qint64 shotId, const QString& reviewId);
    Q_INVOKABLE void releaseShotEdits(qint64 shotId, const QString& reviewId);
    Q_INVOKABLE void uploadShot(qint64 shotId);
    Q_INVOKABLE void refreshVisualizerShot(qint64 shotId);
    Q_INVOKABLE QString decentShotViewUrl(const QString& serial, const QString& serverShotId) const;
    Q_INVOKABLE QString requestShotProse(const QVariantMap& shot);
    Q_INVOKABLE bool copyToClipboard(const QString& text) const;
    Q_INVOKABLE bool openExternalUrl(const QString& url) const;
    Q_INVOKABLE void setRefractometerHunt(bool active, const QString& reviewId);
    Q_INVOKABLE void scanForDevices(const QString& reviewId);
    Q_INVOKABLE void requestRefractometerMeasurement(const QString& reviewId);
signals:
    void stateChanged();
    void actionCompleted(const QString& requestId, const QString& operation,
                         bool success, const QVariantMap& result, const QString& reason);
    void carryForwardInvalidated(const QString& reviewId);
    void visualizerUploadSucceeded(qint64 shotId, const QString& visualizerId, const QString& url);
    void savedShotFinished(qint64 shotId, const QString& error, const QString& skipReason);
    void decentUploadFinished(qint64 shotId, int result);
    void refractometerMeasurementComplete(const QString& reviewId);
    void refractometerAverageProgress(const QString& reviewId, int completed, int total);
    void refractometerReading(const QString& reviewId, double tds);
    void refractometerMeasuringChanged();
private:
    friend struct ShotReviewActionsForeign;
    explicit ShotReviewActions(QRemoteObjectNode* node, QObject* parent = nullptr);
    static QRemoteObjectNode* publishedNode();
    QString send(const QString& operation, const QVariantMap& arguments);
    QPointer<ShotReviewActionStateReplica> m_state;
    QPointer<QRemoteObjectNode> m_node;
    QMetaObject::Connection m_nodeDestroyed;
    QHash<QString, QString> m_pending;
    quint64 m_generation = 0;
    QString m_clientId;
    void failPending(const QString& reason);
};
