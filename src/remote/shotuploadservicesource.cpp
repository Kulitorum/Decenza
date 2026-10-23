#include "shotuploadservicesource.h"
#include "shotuploadcommandpolicy.h"

#include "core/settings.h"
#include "core/settings_visualizer.h"
#include "network/accountlink.h"
#include "network/decentaccount.h"
#include "network/shotuploads.h"
#include "network/visualizerimporter.h"
#include "network/visualizeruploader.h"

#include <QMetaType>
#include <QRegularExpression>
#include <QUuid>
#include <QtGlobal>

namespace {
QString startReason(ServiceRequestIds::Start start)
{
    switch (start) {
    case ServiceRequestIds::Start::InvalidId: return QStringLiteral("invalidRequest");
    case ServiceRequestIds::Start::DuplicateId: return QStringLiteral("duplicateRequest");
    case ServiceRequestIds::Start::LimitReached: return QStringLiteral("busy");
    case ServiceRequestIds::Start::Accepted: break;
    }
    return {};
}

QString safeDecentState(DecentAccount::State state)
{
    switch (state) {
    case DecentAccount::State::Linked: return QStringLiteral("linked");
    case DecentAccount::State::NeedsSignIn: return QStringLiteral("needsSignIn");
    case DecentAccount::State::NotLinked: break;
    }
    return QStringLiteral("notLinked");
}

QVariantMap safeMissing(const QVariantMap& all, const QString& destination)
{
    const QVariantMap value = all.value(destination).toMap();
    static const QStringList statuses{
        QStringLiteral("slowedDown"), QStringLiteral("waitingForMachine"),
        QStringLiteral("waitingForSerial"), QStringLiteral("uploading"),
        QStringLiteral("failedBefore")};
    const QString candidate = value.value(QStringLiteral("status")).toString();
    bool ok = false;
    const qlonglong resume = value.value(QStringLiteral("resumeAtMs")).toLongLong(&ok);
    QVariantMap result{
        {QStringLiteral("count"), qBound(0, value.value(QStringLiteral("count")).toInt(), 100000)},
        {QStringLiteral("failed"), qBound(0, value.value(QStringLiteral("failed")).toInt(), 100000)},
        {QStringLiteral("running"), value.value(QStringLiteral("running")).toBool()},
        {QStringLiteral("done"), qBound(0, value.value(QStringLiteral("done")).toInt(), 100000)},
        {QStringLiteral("total"), qBound(0, value.value(QStringLiteral("total")).toInt(), 100000)},
        {QStringLiteral("resumeAtMs"), ok && resume > 0 ? resume : 0},
        {QStringLiteral("status"), statuses.contains(candidate) ? candidate : QString()}};
    return result;
}

QVariantMap counts(int total, int imported, int skipped, int failed)
{
    return {{QStringLiteral("total"), qMax(0, total)},
            {QStringLiteral("imported"), qMax(0, imported)},
            {QStringLiteral("skipped"), qMax(0, skipped)},
            {QStringLiteral("failed"), qMax(0, failed)}};
}
} // namespace

NativeShotUploadServiceSource::NativeShotUploadServiceSource(
    Settings* settings, VisualizerUploader* visualizer, DecentAccount* decent,
    VisualizerImporter* importer, ShotUploads* uploads, QObject* parent)
    : ShotUploadStateSimpleSource(parent)
    , m_settings(settings), m_visualizer(visualizer), m_decent(decent)
    , m_importer(importer), m_uploads(uploads)
{
    if (m_settings) {
        connect(m_settings->visualizer(), &SettingsVisualizer::visualizerActiveChanged,
                this, &NativeShotUploadServiceSource::refreshSnapshot);
    }
    if (m_visualizer) {
        connect(m_visualizer, &VisualizerUploader::accountConnectFinished, this,
                [this](AccountLink::Error error) {
            const QString id = m_visualizerLinkId;
            m_visualizerLinkId.clear();
            if (!id.isEmpty())
                complete(id, QStringLiteral("connectVisualizer"), true,
                         {{QStringLiteral("errorCode"), static_cast<int>(error)}});
            refreshSnapshot();
        });
        connect(m_visualizer, &VisualizerUploader::connectingChanged, this,
                &NativeShotUploadServiceSource::refreshSnapshot);
    }
    if (m_decent) {
        connect(m_decent, &DecentAccount::linkFinished, this, [this](AccountLink::Error error) {
            const QString id = m_decentLinkId;
            m_decentLinkId.clear();
            if (!id.isEmpty()) {
                if (error == AccountLink::Error::None)
                    m_decentChoiceOwnerId = id;
                else
                    m_decentChoiceOwnerId.clear();
                complete(id, QStringLiteral("linkDecent"), true,
                         {{QStringLiteral("errorCode"), static_cast<int>(error)}});
            }
            refreshSnapshot();
        });
        connect(m_decent, &DecentAccount::stateChanged, this,
                &NativeShotUploadServiceSource::refreshSnapshot);
        connect(m_decent, &DecentAccount::busyChanged, this,
                &NativeShotUploadServiceSource::refreshSnapshot);
        connect(m_decent, &DecentAccount::machineChoiceNeeded, this,
                [this](const QStringList& serials, const QStringList& labels) {
            const QString requestId = m_decentChoiceOwnerId;
            m_decentChoiceOwnerId.clear();
            if (!requestId.isEmpty()) {
                QStringList safeSerials;
                QStringList safeLabels;
                const qsizetype limit = qMin<qsizetype>(32, qMin(serials.size(), labels.size()));
                for (qsizetype i = 0; i < limit; ++i) {
                    if (serials.at(i).size() > 64 || labels.at(i).size() > 128)
                        continue;
                    safeSerials.append(serials.at(i));
                    safeLabels.append(labels.at(i));
                }
                if (!safeSerials.isEmpty())
                    emit machineChoiceNeeded(requestId, safeSerials, safeLabels);
            }
        });
        connect(m_decent, &DecentAccount::accountUrlReady, this,
                [this](const QString& requestId, const QUrl& url) {
            const QString receipt = m_decentAccountUrlRequests.take(requestId);
            if (receipt.isEmpty()) return;
            const QString encodedUrl = url.toString(QUrl::FullyEncoded);
            if (encodedUrl.isEmpty() || encodedUrl.size() > 4096) {
                complete(requestId, QStringLiteral("openDecentAccount"), false, {},
                         QStringLiteral("privateResultUnavailable"));
                return;
            }
            while (m_privateResults.size() >= 16 && !m_privateResultOrder.isEmpty())
                m_privateResults.remove(m_privateResultOrder.dequeue());
            if (m_privateResults.size() >= 16) {
                complete(requestId, QStringLiteral("openDecentAccount"), false, {},
                         QStringLiteral("privateResultUnavailable"));
                return;
            }
            m_privateResults.insert(receipt, {{QStringLiteral("url"), encodedUrl}});
            m_privateResultOrder.enqueue(receipt);
            complete(requestId, QStringLiteral("openDecentAccount"), true);
        });
    }
    if (m_importer) {
        connect(m_importer, &VisualizerImporter::recoveringChanged, this,
                &NativeShotUploadServiceSource::refreshSnapshot);
        connect(m_importer, &VisualizerImporter::recoveryProgress, this,
                [this](int total, int imported, int skipped, int failed) {
            if (!m_recoveryId.isEmpty())
                emit recoveryProgress(m_recoveryId, total, imported, skipped, failed);
        });
        connect(m_importer, &VisualizerImporter::recoveryComplete, this,
                [this](int total, int imported, int skipped, int failed) {
            const QString id = m_recoveryId;
            m_recoveryId.clear();
            if (!id.isEmpty()) {
                emit recoveryComplete(id, total, imported, skipped, failed);
                complete(id, QStringLiteral("recoverVisualizer"), true,
                         counts(total, imported, skipped, failed));
            }
            refreshSnapshot();
        });
        connect(m_importer, &VisualizerImporter::recoveryFailed, this,
                [this](const QString& message) {
            const QString id = m_recoveryId;
            m_recoveryId.clear();
            QString safeMessage = message.left(512);
            safeMessage.replace(QRegularExpression(QStringLiteral("[\\x00-\\x1f]")),
                                QStringLiteral(" "));
            if (!id.isEmpty())
                complete(id, QStringLiteral("recoverVisualizer"), false,
                         {{QStringLiteral("error"), safeMessage}},
                         QStringLiteral("recoveryFailed"));
            refreshSnapshot();
        });
    }
    if (m_uploads)
        connect(m_uploads, &ShotUploads::missingChanged, this,
                &NativeShotUploadServiceSource::refreshSnapshot);
    refreshSnapshot();
}

QVariantMap NativeShotUploadServiceSource::takePrivateResult(QString receipt)
{
    const QUuid parsed = QUuid::fromString(receipt);
    if (parsed.isNull() || receipt != parsed.toString(QUuid::WithoutBraces).toLower())
        return {};
    const auto it = m_privateResults.find(receipt);
    if (it == m_privateResults.end())
        return {};
    const QVariantMap result = it.value();
    m_privateResults.erase(it);
    m_privateResultOrder.removeOne(receipt);
    return result;
}

void NativeShotUploadServiceSource::refresh()
{
    refreshSnapshot();
}

QVariantMap NativeShotUploadServiceSource::makeSnapshot() const
{
    if (!m_settings)
        return {};
    const bool visualizerConnected = m_settings->visualizer()->visualizerConnected();
    const bool visualizerBusy = m_visualizer && m_visualizer->connecting();
    const QString decentState = m_decent
        ? safeDecentState(m_decent->state()) : QStringLiteral("notLinked");
    QVariantMap missing;
    if (m_uploads) {
        const QVariantMap all = m_uploads->missing();
        missing.insert(QStringLiteral("visualizer"), safeMissing(all, QStringLiteral("visualizer")));
        missing.insert(QStringLiteral("decent"), safeMissing(all, QStringLiteral("decent")));
    }
    return {
        {QStringLiteral("visualizer"), QVariantMap{
            {QStringLiteral("connected"), visualizerConnected},
            {QStringLiteral("busy"), visualizerBusy},
            {QStringLiteral("username"), m_settings->visualizer()->visualizerUsername().left(320)}}},
        {QStringLiteral("decent"), QVariantMap{
            {QStringLiteral("state"), decentState},
            {QStringLiteral("busy"), m_decent && m_decent->busy()},
            {QStringLiteral("email"), m_decent ? m_decent->email().left(320) : QString()}}},
        {QStringLiteral("missing"), missing},
        {QStringLiteral("importer"), QVariantMap{
            {QStringLiteral("recovering"), m_importer && m_importer->isRecovering()}}}
    };
}

void NativeShotUploadServiceSource::refreshSnapshot()
{
    setSnapshot(makeSnapshot());
    setReady(m_settings && m_visualizer && m_decent && m_importer && m_uploads);
}

void NativeShotUploadServiceSource::complete(const QString& requestId,
                                              const QString& operation, bool success,
                                              const QVariantMap& result,
                                              const QString& reason)
{
    if (!m_requests.finish(requestId))
        return;
    emit operationCompleted(requestId, operation, success, result, reason);
}

void NativeShotUploadServiceSource::execute(QString requestId, QString operation,
                                             QVariantMap arguments)
{
    const auto start = m_requests.begin(requestId);
    if (start != ServiceRequestIds::Start::Accepted) {
        emit operationCompleted(requestId, operation, false, {}, startReason(start));
        return;
    }
    ShotUploadCommandPolicy::Command command;
    QString reason;
    if (!ShotUploadCommandPolicy::decode(operation, arguments, &command, &reason)) {
        m_requests.finish(requestId);
        emit operationCompleted(requestId, operation, false, {}, reason);
        return;
    }
    if (!m_settings || !m_visualizer || !m_decent || !m_importer || !m_uploads) {
        complete(requestId, operation, false, {}, QStringLiteral("unavailable"));
        return;
    }

    if (operation == QLatin1String("connectVisualizer")) {
        if (m_visualizer->connecting() || !m_visualizerLinkId.isEmpty()) {
            complete(requestId, operation, false, {}, QStringLiteral("busy"));
            return;
        }
        m_visualizerLinkId = requestId;
        const QVariantMap args = command.arguments;
        m_visualizer->connectAccount(args.value(QStringLiteral("identity")).toString().trimmed(),
                                     args.value(QStringLiteral("password")).toString());
        refreshSnapshot();
        return;
    }
    if (operation == QLatin1String("linkDecent")) {
        if (m_decent->busy() || !m_decentLinkId.isEmpty()) {
            complete(requestId, operation, false, {}, QStringLiteral("busy"));
            return;
        }
        m_decentLinkId = requestId;
        const QVariantMap args = command.arguments;
        m_decent->link(args.value(QStringLiteral("email")).toString().trimmed(),
                       args.value(QStringLiteral("password")).toString());
        refreshSnapshot();
        return;
    }
    if (operation == QLatin1String("recoverVisualizer")) {
        if (m_importer->isRecovering() || !m_recoveryId.isEmpty()) {
            complete(requestId, operation, false, {}, QStringLiteral("busy"));
            return;
        }
        m_recoveryId = requestId;
        m_importer->recoverShots(command.arguments.value(QStringLiteral("fromEpoch")).toLongLong(),
                                 command.arguments.value(QStringLiteral("toEpoch")).toLongLong());
        refreshSnapshot();
        return;
    }
    if (operation == QLatin1String("chooseDecentMachine")) {
        const QString serial = command.arguments.value(QStringLiteral("serial")).toString();
        if (!m_decent->isRegisteredDe1Choice(serial)) {
            complete(requestId, operation, false, {}, QStringLiteral("unregisteredMachine"));
            return;
        }
        m_decent->chooseMachine(serial);
        complete(requestId, operation, true);
        return;
    }
    if (operation == QLatin1String("uploadMissing")) {
        const QString destination = command.arguments.value(QStringLiteral("destination")).toString();
        const bool started = m_uploads->uploadMissing(destination);
        complete(requestId, operation, started,
                 started ? QVariantMap{{QStringLiteral("destination"), destination}} : QVariantMap(),
                 started ? QString() : QStringLiteral("notStarted"));
        refreshSnapshot();
        return;
    }
    if (operation == QLatin1String("disconnectVisualizer")) {
        m_visualizer->disconnectAccount();
        complete(requestId, operation, true);
    } else if (operation == QLatin1String("unlinkDecent")) {
        m_decentChoiceOwnerId.clear();
        m_decent->unlink();
        complete(requestId, operation, true);
    } else if (operation == QLatin1String("openDecentAccount")) {
        const QString receipt = command.arguments.value(QStringLiteral("receipt")).toString();
        if (m_privateResults.contains(receipt)
            || m_decentAccountUrlRequests.size() >= 16
            || m_decentAccountUrlRequests.values().contains(receipt)) {
            complete(requestId, operation, false, {}, QStringLiteral("duplicateReceipt"));
            return;
        }
        m_decentAccountUrlRequests.insert(requestId, receipt);
        m_decent->requestAccountUrl(requestId);
        return;
    }
    refreshSnapshot();
}
