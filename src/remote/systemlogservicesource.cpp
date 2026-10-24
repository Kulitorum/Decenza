#include "systemlogservicesource.h"

#include "network/webdebuglogger.h"

#include <QRegularExpression>

namespace {
constexpr int kMaximumMarkers = 8;
constexpr int kMaximumMarkerLength = 64;
constexpr int kMaximumSnapshotLines = 1000;
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

    // The history is a bounded host-thread read; raw live lines are sent through
    // one signal and filtered independently by each client view.
    const QVariantMap snapshot = m_logger->sessionSnapshotMatching(markers, minLevel, maxLines);
    emit snapshotCompleted(requestId, true,
                           snapshot.value(QStringLiteral("lines")).toStringList(),
                           snapshot.value(QStringLiteral("sequence")).toLongLong(), {});
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
