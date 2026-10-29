#include "shotreviewactions.h"

#include "rep_shotreviewactions_replica.h"
#include "network/decentshoturl.h"

#include <QMetaObject>
#include <QRemoteObjectNode>
#include <QRemoteObjectPendingCallWatcher>
#include <QUuid>
#include <QGuiApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QUrl>

namespace {
QPointer<QRemoteObjectNode> s_publishedNode;
}

ShotReviewActions::ShotReviewActions(QRemoteObjectNode* node, QObject* parent)
    : RemoteService(parent)
{
    setNode(node);
}

ShotReviewActions::~ShotReviewActions()
{
    if (m_state)
        disconnect(m_state, nullptr, this, nullptr);
}

void ShotReviewActions::setPublishedNode(QRemoteObjectNode* node)
{
    s_publishedNode = node;
}

QRemoteObjectNode* ShotReviewActions::publishedNode()
{
    return s_publishedNode;
}

void ShotReviewActions::setNode(QRemoteObjectNode* node)
{
    if (m_node == node && m_state)
        return;
    disconnect(m_nodeDestroyed);
    m_nodeDestroyed = {};
    ++m_generation;
    failPending(QStringLiteral("Review action interrupted; it was not retried."));
    if (m_state) {
        auto* old = m_state.data();
        disconnect(m_state, nullptr, this, nullptr);
        old->deleteLater();
    }
    m_state = nullptr;
    m_node = node;
    if (node) {
        m_clientId = node->property("decenzaClientId").toString();
        if (m_clientId.isEmpty()) m_clientId = newRequestId();
        m_nodeDestroyed = connect(node, &QObject::destroyed, this, [this] { setNode(nullptr); });
        m_state = node->acquire<ShotReviewActionStateReplica>();
        if (m_state) {
            m_state->setParent(this);
            const quint64 generation = m_generation;
            const auto update = [this, generation] {
                if (generation != m_generation)
                    return;
                if (!ready())
                    failPending(QStringLiteral("Review actions disconnected; the operation was not retried."));
                emit stateChanged();
            };
            connect(m_state, &QRemoteObjectReplica::stateChanged, this, update);
            connect(m_state, &QRemoteObjectReplica::notified, this, update);
            connect(m_state, QOverload<bool>::of(
                        &ShotReviewActionStateReplica::visualizerUploadingChanged),
                    this, [update](bool) { update(); });
            connect(m_state, QOverload<bool>::of(
                        &ShotReviewActionStateReplica::decentUploadingChanged),
                    this, [update](bool) { update(); });
            connect(m_state, &ShotReviewActionStateReplica::decentLastShotIdChanged,
                    this, [update](qint64) { update(); });
            connect(m_state, &ShotReviewActionStateReplica::decentLastHttpStatusChanged,
                    this, [update](int) { update(); });
            connect(m_state, &ShotReviewActionStateReplica::decentLastSerialChanged,
                    this, [update](const QString&) { update(); });
            connect(m_state, &ShotReviewActionStateReplica::decentResultCodeChanged,
                    this, [update](const QString&) { update(); });
            connect(m_state, QOverload<QString>::of(
                        &ShotReviewActionStateReplica::visualizerLastUploadStatusChanged),
                    this, [update](const QString&) { update(); });
            connect(m_state, &ShotReviewActionStateReplica::decentUploadedResultChanged,
                    this, [update](int) { update(); });
            connect(m_state, &ShotReviewActionStateReplica::visualizerActiveChanged,
                    this, [update](bool) { update(); });
            connect(m_state, &ShotReviewActionStateReplica::decentActiveChanged,
                    this, [update](bool) { update(); });
            connect(m_state, &ShotReviewActionStateReplica::aiConfiguredChanged,
                    this, [update](bool) { update(); });
            connect(m_state, &ShotReviewActionStateReplica::aiAnalyzingChanged,
                    this, [update](bool) { update(); });
            connect(m_state, &ShotReviewActionStateReplica::discussShotUrlChanged,
                    this, [update](const QString&) { update(); });
            connect(m_state, &ShotReviewActionStateReplica::discussReadyChanged,
                    this, [update](bool) { update(); });
            connect(m_state, &ShotReviewActionStateReplica::discussEnabledChanged,
                    this, [update](bool) { update(); });
            connect(m_state, &ShotReviewActionStateReplica::discussAppReadsShotChanged,
                    this, [update](bool) { update(); });
            connect(m_state, &ShotReviewActionStateReplica::refractometerConfiguredChanged,
                    this, [update](bool) { update(); });
            connect(m_state, &ShotReviewActionStateReplica::refractometerConnectedChanged,
                    this, [update](bool) { update(); });
            connect(m_state, QOverload<bool>::of(
                        &ShotReviewActionStateReplica::refractometerMeasuringChanged),
                    this, [this, update](bool) {
                        const QPointer<ShotReviewActions> guard(this);
                        update();
                        if (!guard)
                            return;
                        emit refractometerMeasuringChanged();
                    });
            connect(m_state, &ShotReviewActionStateReplica::refractometerTdsChanged,
                    this, [update](double) { update(); });
            connect(m_state, &ShotReviewActionStateReplica::refractometerTemperatureChanged,
                    this, [update](double) { update(); });
            connect(m_state, &ShotReviewActionStateReplica::refractometerNameChanged,
                    this, [update](const QString&) { update(); });
            connect(m_state, &ShotReviewActionStateReplica::savedRefractometerAddressChanged,
                    this, [update](const QString&) { update(); });
            connect(m_state, &ShotReviewActionStateReplica::savedRefractometerNameChanged,
                    this, [update](const QString&) { update(); });
            connect(m_state, &ShotReviewActionStateReplica::refractometerOwnerReviewIdChanged,
                    this, [update](const QString&) { update(); });
            connect(m_state, &ShotReviewActionStateReplica::visualizerUploadSucceeded,
                    this, &ShotReviewActions::visualizerUploadSucceeded);
            connect(m_state, &ShotReviewActionStateReplica::carryForwardInvalidated,
                    this, &ShotReviewActions::carryForwardInvalidated);
            connect(m_state, &ShotReviewActionStateReplica::savedShotFinished,
                    this, &ShotReviewActions::savedShotFinished);
            connect(m_state, &ShotReviewActionStateReplica::decentUploadFinished,
                    this, &ShotReviewActions::decentUploadFinished);
            connect(m_state, &ShotReviewActionStateReplica::refractometerMeasurementComplete,
                    this, &ShotReviewActions::refractometerMeasurementComplete);
            connect(m_state, &ShotReviewActionStateReplica::refractometerAverageProgress,
                    this, &ShotReviewActions::refractometerAverageProgress);
            connect(m_state, &ShotReviewActionStateReplica::refractometerReading,
                    this, &ShotReviewActions::refractometerReading);
            connect(m_state, &ShotReviewActionStateReplica::actionCompleted, this,
                    [this](const QString& id, const QString& op, bool success,
                           const QVariantMap& result, const QString& reason) {
                        if (!m_pending.contains(id) || m_pending.value(id) != op)
                            return;
                        m_pending.remove(id);
                        emit actionCompleted(id, op, success, result, reason);
                    }, Qt::QueuedConnection);
        }
    }
    emit stateChanged();
}

bool ShotReviewActions::ready() const
{
    return RemoteServiceReplica::isReady(m_node, m_state, publishedNode());
}

bool ShotReviewActions::visualizerUploading() const
{
    return ready() && m_state->visualizerUploading();
}

bool ShotReviewActions::decentUploading() const
{
    return ready() && m_state->decentUploading();
}

qint64 ShotReviewActions::decentLastShotId() const
{
    return ready() ? m_state->decentLastShotId() : 0;
}

int ShotReviewActions::decentLastHttpStatus() const
{
    return ready() ? m_state->decentLastHttpStatus() : 0;
}

QString ShotReviewActions::decentLastSerial() const
{
    return ready() ? m_state->decentLastSerial() : QString();
}

QString ShotReviewActions::decentResultCode() const
{
    return ready() ? m_state->decentResultCode() : QString();
}

QString ShotReviewActions::visualizerLastUploadStatus() const
{
    return ready() ? m_state->visualizerLastUploadStatus() : QString();
}

bool ShotReviewActions::visualizerActive() const { return ready() && m_state->visualizerActive(); }
bool ShotReviewActions::decentActive() const { return ready() && m_state->decentActive(); }
bool ShotReviewActions::aiConfigured() const { return ready() && m_state->aiConfigured(); }
bool ShotReviewActions::aiAnalyzing() const { return ready() && m_state->aiAnalyzing(); }
bool ShotReviewActions::discussEnabled() const { return ready() && m_state->discussEnabled(); }
QString ShotReviewActions::discussShotUrl() const { return ready() ? m_state->discussShotUrl() : QString(); }
bool ShotReviewActions::discussReady() const { return ready() && m_state->discussReady(); }
bool ShotReviewActions::discussAppReadsShot() const { return ready() && m_state->discussAppReadsShot(); }
bool ShotReviewActions::refractometerConfigured() const { return ready() && m_state->refractometerConfigured(); }
bool ShotReviewActions::refractometerConnected() const { return ready() && m_state->refractometerConnected(); }
bool ShotReviewActions::refractometerMeasuring() const { return ready() && m_state->refractometerMeasuring(); }
double ShotReviewActions::refractometerTds() const { return ready() ? m_state->refractometerTds() : 0.0; }
double ShotReviewActions::refractometerTemperature() const { return ready() ? m_state->refractometerTemperature() : 0.0; }
QString ShotReviewActions::refractometerName() const { return ready() ? m_state->refractometerName() : QString(); }
QString ShotReviewActions::savedRefractometerAddress() const { return ready() ? m_state->savedRefractometerAddress() : QString(); }
QString ShotReviewActions::savedRefractometerName() const { return ready() ? m_state->savedRefractometerName() : QString(); }
QString ShotReviewActions::refractometerOwnerReviewId() const { return ready() ? m_state->refractometerOwnerReviewId() : QString(); }

int ShotReviewActions::decentUploadedResult() const
{
    return ready() ? m_state->decentUploadedResult() : 1;
}

QString ShotReviewActions::newRequestId() const
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
}

QString ShotReviewActions::send(const QString& operation, const QVariantMap& arguments)
{
    if (!ready() || m_pending.size() >= 64) {
        QMetaObject::invokeMethod(this, [this, operation] {
            emit actionCompleted({}, operation, false, {}, QStringLiteral("Review actions are unavailable."));
        }, Qt::QueuedConnection);
        return {};
    }
    const QString id = newRequestId();
    m_pending.insert(id, operation);
    QVariantMap ownedArguments = arguments;
    ownedArguments.insert(QStringLiteral("clientId"), m_clientId);
    m_state->execute(id, operation, ownedArguments);
    return id;
}

QString ShotReviewActions::executeFileOperation(const QString& operation, const QVariantMap& arguments)
{
    return send(operation, arguments);
}

void ShotReviewActions::failPending(const QString& reason)
{
    QHash<QString, QString> pending;
    pending.swap(m_pending);
    for (auto it = pending.cbegin(); it != pending.cend(); ++it) {
        const QString requestId = it.key();
        const QString operation = it.value();
        QMetaObject::invokeMethod(this, [this, requestId, operation, reason] {
            emit actionCompleted(requestId, operation, false, {}, reason);
        }, Qt::QueuedConnection);
    }
}

QString ShotReviewActions::holdShotEdits(qint64 shotId, const QString& reviewId)
{
    return send(QStringLiteral("hold"), {{QStringLiteral("shotId"), shotId}, {QStringLiteral("reviewId"), reviewId}});
}

void ShotReviewActions::expectHeldShotEdit(qint64 shotId, const QString& reviewId)
{
    send(QStringLiteral("expectHeldEdit"), {{QStringLiteral("shotId"), shotId}, {QStringLiteral("reviewId"), reviewId}});
}

void ShotReviewActions::releaseShotEdits(qint64 shotId, const QString& reviewId)
{
    send(QStringLiteral("release"), {{QStringLiteral("shotId"), shotId}, {QStringLiteral("reviewId"), reviewId}});
}

void ShotReviewActions::uploadShot(qint64 shotId)
{
    send(QStringLiteral("upload"), {{QStringLiteral("shotId"), shotId}});
}

void ShotReviewActions::refreshVisualizerShot(qint64 shotId)
{
    send(QStringLiteral("refreshVisualizer"), {{QStringLiteral("shotId"), shotId}});
}

QString ShotReviewActions::decentShotViewUrl(const QString& serial, const QString& serverShotId) const
{
    return DecentShotUrl::shotViewUrl(serial, serverShotId);
}

QString ShotReviewActions::requestShotProse(const QVariantMap& shot)
{
    return send(QStringLiteral("prose"), {{QStringLiteral("shot"), shot}});
}

bool ShotReviewActions::copyToClipboard(const QString& text) const
{
    if (!QGuiApplication::clipboard())
        return false;
    QGuiApplication::clipboard()->setText(text);
    return true;
}

bool ShotReviewActions::openExternalUrl(const QString& text) const
{
    const QUrl url(text);
    return url.isValid() && !url.scheme().isEmpty() && QDesktopServices::openUrl(url);
}

void ShotReviewActions::setRefractometerHunt(bool active, const QString& reviewId)
{ send(QStringLiteral("refractometerHunt"), {{QStringLiteral("active"), active}, {QStringLiteral("reviewId"), reviewId}}); }
void ShotReviewActions::scanForDevices(const QString& reviewId)
{ send(QStringLiteral("scanForDevices"), {{QStringLiteral("reviewId"), reviewId}}); }
void ShotReviewActions::requestRefractometerMeasurement(const QString& reviewId)
{ send(QStringLiteral("requestRefractometerMeasurement"), {{QStringLiteral("reviewId"), reviewId}}); }
