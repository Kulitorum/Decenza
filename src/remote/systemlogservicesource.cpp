#include "systemlogservicesource.h"

#include "network/webdebuglogger.h"

#include <QRegularExpression>
#include <QCoreApplication>
#include <QFile>
#include <QRunnable>
#include <QThreadPool>
#include "core/ziparchive.h"
#include "systemlogfilepolicy.h"
#include <utility>

namespace {
constexpr int kMaximumMarkers = 8;
constexpr int kMaximumMarkerLength = 64;
constexpr int kMaximumSnapshotLines = 1000;
constexpr int kMaximumSnapshotWorkers = 8;
}

NativeSystemLogServiceSource::NativeSystemLogServiceSource(
    WebDebugLogger* logger, QObject* parent)
    : SystemLogStateSimpleSource(parent), m_logger(logger)
{
    setReady(!m_logger.isNull());
    if (!m_logger)
        return;

    connect(m_logger, &WebDebugLogger::lineAppended, this,
            [this](QtMsgType, const QString& line, qint64 sequence) {
                onLineAppended(line, sequence);
            });
    connect(m_logger, &QObject::destroyed, this, [this] {
        m_logger = nullptr;
        setReady(false);
    });
}

bool NativeSystemLogServiceSource::validRequestId(const QString& requestId)
{
    static const QRegularExpression uuid(
        QStringLiteral("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-"
                       "[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$"));
    return uuid.match(requestId).hasMatch();
}

QString NativeSystemLogServiceSource::validateFilter(const QStringList& markers,
                                                      const QString& minLevel,
                                                      int maxLines)
{
    if (markers.isEmpty() || markers.size() > kMaximumMarkers)
        return QStringLiteral("Select between 1 and 8 log markers.");
    for (const QString& marker : markers) {
        if (marker.size() < 3 || marker.size() > kMaximumMarkerLength
            || !marker.startsWith(QLatin1Char('['))
            || !marker.endsWith(QLatin1Char(']'))
            || marker.contains(QLatin1Char('\n'))
            || marker.contains(QLatin1Char('\r'))) {
            return QStringLiteral("A log marker is invalid.");
        }
    }
    static const QStringList levels = {
        QStringLiteral(""), QStringLiteral("DEBUG"), QStringLiteral("INFO"),
        QStringLiteral("WARN"), QStringLiteral("ERROR"), QStringLiteral("FATAL")
    };
    if (!levels.contains(minLevel))
        return QStringLiteral("The minimum log level is invalid.");
    if (maxLines < 1 || maxLines > kMaximumSnapshotLines)
        return QStringLiteral("The requested log snapshot limit is invalid.");
    return {};
}

void NativeSystemLogServiceSource::requestSnapshot(QString requestId,
                                                    QStringList markers,
                                                    QString minLevel,
                                                    int maxLines)
{
    if (!validRequestId(requestId)) {
        emit snapshotCompleted(requestId, false, {}, 0,
                               QStringLiteral("The log view request ID is invalid."));
        return;
    }
    if (!m_logger) {
        emit snapshotCompleted(requestId, false, {}, 0,
                               QStringLiteral("The host log is unavailable."));
        return;
    }
    const QString invalid = validateFilter(markers, minLevel, maxLines);
    if (!invalid.isEmpty()) {
        emit snapshotCompleted(requestId, false, {}, 0, invalid);
        return;
    }

    if (!QCoreApplication::instance()) {
        emit snapshotCompleted(requestId, false, {}, 0, QStringLiteral("The host log is unavailable."));
        return;
    }
    if (m_snapshotWorkers >= kMaximumSnapshotWorkers) {
        emit snapshotCompleted(requestId, false, {}, 0, QStringLiteral("Too many log snapshot requests."));
        return;
    }
    // Near-2 MiB sparse fixtures measured 19.7/66.9 ms (median/worst) for
    // 400 lines and 37.5/128.1 ms for 1000 lines on the host thread. Capture
    // bytes+fence on a worker and filter outside the append lock instead.
    const auto reader = m_logger->sessionSnapshotReader(markers, minLevel, maxLines,
                                                        FileBundlePolicy::MaximumLogBytes);
    ++m_snapshotWorkers;
    const QPointer<NativeSystemLogServiceSource> self(this);
    QObject* context = QCoreApplication::instance();
    // The global pool is joined before the application dispatcher is destroyed
    // (Qt 6.12, qtbase/src/corelib/kernel/qcoreapplication.cpp:959-968).
    QThreadPool::globalInstance()->start(QRunnable::create([reader, self, context, requestId] {
        const auto snapshot = reader();
        QMetaObject::invokeMethod(context, [self, requestId, snapshot] {
            if (!self) return;
            --self->m_snapshotWorkers;
            if (!self->m_logger) {
                emit self->snapshotCompleted(requestId, false, {}, 0,
                                             QStringLiteral("The host log is unavailable."));
                return;
            }
            emit self->snapshotCompleted(requestId, snapshot.reason.isEmpty(), snapshot.lines,
                                         snapshot.sequence, snapshot.reason);
        }, Qt::QueuedConnection);
    }));
}

void NativeSystemLogServiceSource::requestBoundary(QString requestId)
{
    if (!validRequestId(requestId)) {
        emit boundaryCompleted(requestId, false, 0,
                               QStringLiteral("The log view request ID is invalid."));
        return;
    }
    if (!m_logger) {
        emit boundaryCompleted(requestId, false, 0,
                               QStringLiteral("The host log is unavailable."));
        return;
    }
    emit boundaryCompleted(requestId, true, m_logger->writtenSequence(), {});
}

void NativeSystemLogServiceSource::onLineAppended(const QString& line,
                                                   qint64 sequence)
{
    if (!m_logger)
        return;
    // Forward exactly one untouched line. Qt Remote Objects owns transport fanout;
    // there is no marker parsing, per-view state, or client request work on the
    // host's logging path.
    emit lineAppended(line, sequence);
}

void NativeSystemLogServiceSource::openClient(const QString& clientId)
{
    if (!clientId.isEmpty() && clientId.size() <= 128) m_clients.insert(clientId);
}

void NativeSystemLogServiceSource::finish(QString id, QString operation, bool success,
                                          const QVariantMap& result, const QString& reason)
{
    if (m_requests.finish(id)) emit operationCompleted(id, operation, success, result, reason);
}

void NativeSystemLogServiceSource::clearBundle()
{
    if (m_bundles.hasBundle()) {
        QString ignored;
        m_bundles.release({{QStringLiteral("bundleId"), m_bundleId}}, &ignored);
    }
    m_bundleId.clear();
    m_bundleClient.clear();
    m_bundleRequest.clear();
}

void NativeSystemLogServiceSource::cancelPreparation()
{
    if (m_cancelled) m_cancelled->store(true, std::memory_order_relaxed);
    const QString retired = std::exchange(m_activeId, {});
    m_activeClient.clear();
    if (!retired.isEmpty())
        finish(retired, QStringLiteral("prepareLogDownload"), false, {}, QStringLiteral("cancelled"));
}

void NativeSystemLogServiceSource::closeClient(const QString& clientId)
{
    if (!m_clients.remove(clientId)) return;
    if (m_bundleClient == clientId) clearBundle();
    if (m_activeClient == clientId) cancelPreparation();
}

void NativeSystemLogServiceSource::execute(QString id, QString operation, QVariantMap args)
{
    const QVariant client = args.take(QStringLiteral("clientId"));
    const auto start = m_requests.begin(id);
    if (start == ServiceRequestIds::Start::DuplicateId) return;
    if (start != ServiceRequestIds::Start::Accepted) {
        emit operationCompleted(id, operation, false, {},
            start == ServiceRequestIds::Start::InvalidId ? QStringLiteral("invalidRequest")
                                                        : QStringLiteral("tooManyRequests"));
        return;
    }
    if (!SystemLogFilePolicy::supported(operation)) {
        finish(id, operation, false, {}, QStringLiteral("unsupportedOperation")); return;
    }
    if (!SystemLogFilePolicy::validArguments(operation, args)) {
        finish(id, operation, false, {}, QStringLiteral("invalidArguments")); return;
    }
    const QString clientId = client.toString();
    if (client.metaType().id() != QMetaType::QString || !m_clients.contains(clientId)) {
        finish(id, operation, false, {}, QStringLiteral("clientUnavailable")); return;
    }
    if (!m_bundles.hasBundle()) clearBundle();
    if (operation == QLatin1String("prepareLogDownload")) {
        prepareDownload(id, clientId, args.value(QStringLiteral("format")).toString()); return;
    }
    if (operation == QLatin1String("cancelFileOperation")) {
        const QString target = args.value(QStringLiteral("requestId")).toString();
        const bool active = target == m_activeId && clientId == m_activeClient;
        const bool bundled = target == m_bundleRequest && clientId == m_bundleClient;
        if (bundled) clearBundle();
        const QPointer<NativeSystemLogServiceSource> guard(this);
        if (active) cancelPreparation();
        if (guard) finish(id, operation, true, {{QStringLiteral("cancelled"), active || bundled}});
        return;
    }
    if (clientId != m_bundleClient || args.value(QStringLiteral("bundleId")).toString() != m_bundleId) {
        finish(id, operation, false, {}, QStringLiteral("invalidBundle")); return;
    }
    QString reason;
    if (operation == QLatin1String("releaseBundle")) {
        clearBundle();
        finish(id, operation, true);
    } else {
        const QVariantMap result = m_bundles.readChunk(args, &reason);
        finish(id, operation, reason.isEmpty(), result, reason);
    }
}

void NativeSystemLogServiceSource::prepareDownload(const QString& id, const QString& clientId,
                                                   const QString& format)
{
    if (!m_logger || !QCoreApplication::instance()) {
        finish(id, QStringLiteral("prepareLogDownload"), false, {}, QStringLiteral("unavailable")); return;
    }
    if (m_workerInFlight || !m_activeId.isEmpty() || m_bundles.hasBundle()) {
        finish(id, QStringLiteral("prepareLogDownload"), false, {}, QStringLiteral("busy")); return;
    }
    auto directory = std::make_shared<QTemporaryDir>();
    if (!directory->isValid()) {
        finish(id, QStringLiteral("prepareLogDownload"), false, {}, QStringLiteral("storageUnavailable")); return;
    }
    const auto reader = m_logger->persistedLogReader(FileBundlePolicy::MaximumLogBytes);
    const QByteArray summary = m_memorySummary ? m_memorySummary().toUtf8() : QByteArray();
    if (summary.size() > FileBundlePolicy::MaximumLogBytes) {
        finish(id, QStringLiteral("prepareLogDownload"), false, {}, QStringLiteral("fileTooLarge")); return;
    }
    const QString bundleId = ServiceOperation::newId().toLower();
    const QString destination = directory->filePath(bundleId + QLatin1Char('.') + format);
    m_activeId = id;
    m_activeClient = clientId;
    m_workerInFlight = true;
    auto cancelled = std::make_shared<std::atomic_bool>(false);
    m_cancelled = cancelled;
    const QPointer<NativeSystemLogServiceSource> self(this);
    QObject* context = QCoreApplication::instance();
    // The global pool is joined before QCoreApplication dies (Qt 6.12,
    // qtbase/src/corelib/kernel/qcoreapplication.cpp:959-968).
    QThreadPool::globalInstance()->start(QRunnable::create(
        [self, context, reader, summary, id, clientId, format, bundleId, directory, destination, cancelled] {
        QByteArray contents;
        QString failure;
        if (cancelled->load(std::memory_order_relaxed)) failure = QStringLiteral("cancelled");
        else if (reader(&contents, &failure)) {
            if (contents.size() + summary.size() > FileBundlePolicy::MaximumLogBytes)
                failure = QStringLiteral("fileTooLarge");
            else contents += summary;
        }
        if (failure.isEmpty() && cancelled->load(std::memory_order_relaxed))
            failure = QStringLiteral("cancelled");
        if (failure.isEmpty()) {
            if (format == QLatin1String("zip")) {
                QuaZip writer(destination);
                const bool written = writer.open(QuaZip::mdCreate)
                    && ZipArchive::addFile(writer, QStringLiteral("debug.log"), contents);
                writer.close();
                if (!written || writer.getZipError() != UNZ_OK) failure = QStringLiteral("archiveFailed");
            } else {
                QFile file(destination);
                if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)
                    || file.write(contents) != contents.size() || !file.flush())
                    failure = QStringLiteral("logWriteFailed");
                else if (contents.isEmpty()) failure = QStringLiteral("logEmpty");
            }
        }
        if (cancelled->load(std::memory_order_relaxed)) failure = QStringLiteral("cancelled");
        QMetaObject::invokeMethod(context,
            [self, id, clientId, format, bundleId, directory, failure] {
            if (self) self->downloadPrepared(id, clientId, format, bundleId, directory, failure);
        }, Qt::QueuedConnection);
    }));
}

void NativeSystemLogServiceSource::downloadPrepared(
    const QString& id, const QString& clientId, const QString& format, const QString& bundleId,
    const std::shared_ptr<QTemporaryDir>& directory, const QString& failure)
{
    m_workerInFlight = false;
    m_cancelled.reset();
    if (m_activeId != id || m_activeClient != clientId || !m_clients.contains(clientId)) return;
    m_activeId.clear();
    m_activeClient.clear();
    QVariantMap result;
    QString reason = failure;
    if (reason.isEmpty()) {
        const bool zip = format == QLatin1String("zip");
        result = m_bundles.adopt(directory, bundleId, bundleId + QLatin1Char('.') + format,
            QStringLiteral("debug.") + format,
            zip ? FileBundlePolicy::MaximumLogBytes * 2 : FileBundlePolicy::MaximumLogBytes,
            zip ? QStringLiteral("application/zip") : QStringLiteral("text/plain"),
            zip ? QStringLiteral("ZIP archives (*.zip)") : QStringLiteral("Log files (*.log)"));
        if (result.isEmpty()) reason = QStringLiteral("bundleInvalid");
        else {
            m_bundleId = bundleId;
            m_bundleClient = clientId;
            m_bundleRequest = id;
        }
    }
    finish(id, QStringLiteral("prepareLogDownload"), reason.isEmpty(), result, reason);
}
