#include "btlogfilter.h"

#include "../ble/blecapability.h"
#include "diagnosticlogging.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QString>
#include <QStringList>

#include <atomic>
#include <utility>

#ifdef Q_OS_ANDROID
#include <dlfcn.h>
#include <unwind.h>
#endif

namespace {
QtMessageHandler g_previousHandler = nullptr;
std::atomic<int> g_gcCrossValidationMessages{0};
QMutex g_gcMutex;
QString g_gcPages;
QString g_batchPages;
QString g_batchTime;
QString g_batchFirst;
QStringList g_batchTypes;
int g_batchCount = 0;
QString g_pendingMismatch;
std::function<void(const QString&)> g_mismatchHandler;

#ifdef Q_OS_ANDROID
struct GcStackCapture { void* frames[32] = {}; int count = 0; };
_Unwind_Reason_Code captureGcFrame(_Unwind_Context* context, void* arg)
{
    auto* capture = static_cast<GcStackCapture*>(arg);
    if (capture->count == 32) return _URC_END_OF_STACK;
    const auto pc = _Unwind_GetIP(context);
    if (pc) capture->frames[capture->count++] = reinterpret_cast<void*>(pc);
    return _URC_NO_REASON;
}
QString mismatchStack()
{
    GcStackCapture capture;
    _Unwind_Backtrace(captureGcFrame, &capture);
    QStringList frames;
    QStringList qmlFrames;
    for (int i = 0; i < capture.count; ++i) {
        Dl_info info{};
        if (dladdr(capture.frames[i], &info) && info.dli_fbase) {
            const QString module = QString::fromLocal8Bit(info.dli_fname).section(QLatin1Char('/'), -1);
            const QString frame = QStringLiteral("%1+0x%2").arg(module).arg(
                quintptr(capture.frames[i]) - quintptr(info.dli_fbase), 0, 16);
            frames.append(frame);
            if (module.contains(QLatin1String("Qt6Qml"))) qmlFrames.append(frame);
        }
    }
    // Put collector frames first so they survive the crash report's short
    // last-message budget; the complete ordered stack remains in debug.log.
    if (!qmlFrames.isEmpty())
        return qmlFrames.join(QLatin1Char(',')) + QLatin1String(" ordered=")
             + frames.join(QLatin1Char(','));
    return frames.join(QLatin1Char(','));
}
#else
QString mismatchStack() { return {}; }
#endif

void deliverGcMismatch()
{
    std::function<void(const QString&)> handler;
    QString mismatch;
    {
        QMutexLocker lock(&g_gcMutex);
        if (!g_mismatchHandler || g_pendingMismatch.isEmpty()) return;
        handler = g_mismatchHandler;
        mismatch = std::move(g_pendingMismatch);
        g_pendingMismatch.clear();
    }
    handler(mismatch);
}

void scheduleGcMismatchDelivery()
{
    if (auto* app = QCoreApplication::instance())
        QMetaObject::invokeMethod(app, deliverGcMismatch, Qt::QueuedConnection);
}

// Qt reports each differing bitmap bit synchronously during its full re-mark.
// A queued delivery groups reports until the event loop runs, without guessing
// collection boundaries from elapsed time. More than one collection may be in a batch.
void recordGcMiss(const QString& msg)
{
    auto* app = QCoreApplication::instance();
    if (!app)
        return; // The existing immediate warning still covers early startup.
    bool schedule = false;
    {
        QMutexLocker lock(&g_gcMutex);
        if (g_batchCount++ == 0) {
            g_batchPages = g_gcPages.isEmpty() ? QStringLiteral("unknown") : g_gcPages;
            g_batchTime = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
            g_batchFirst = msg;
            schedule = true;
        }
        // Keep an example of each reported type, even after the per-run detail
        // cap. Qt supplies a type, not the identity or origin of the object.
        const qsizetype typeAt = msg.lastIndexOf(QLatin1String("With type "));
        const QString type = typeAt < 0 ? QStringLiteral("unknown") : msg.mid(typeAt + 10).left(80);
        if (g_batchTypes.size() < 12 && !g_batchTypes.contains(type))
            g_batchTypes.append(type);
    }
    if (!schedule)
        return;
    QMetaObject::invokeMethod(app, [] {
        int count;
        QString pages, time, first;
        QStringList types;
        {
            QMutexLocker lock(&g_gcMutex);
            count = g_batchCount;
            pages = std::move(g_batchPages);
            time = std::move(g_batchTime);
            first = std::move(g_batchFirst);
            types = std::move(g_batchTypes);
            g_batchTypes.clear();
            g_batchCount = 0;
        }
        DIAG_WARN(MEMORY, "QmlGc")
            << QStringLiteral("Cross-validation batch: firstAt=%1 pages=%2 differences=%3 types=%4 first=%5 "
                              "(grouped until event-loop delivery; may include multiple collections)")
                   .arg(time, pages).arg(count).arg(types.join(QLatin1Char(',')), first);
    }, Qt::QueuedConnection);
}

void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    if (type == QtCriticalMsg && msg.startsWith(QLatin1String("[Memory][QmlGc] DEFERRED_ABORT "))) {
        const QString stack = mismatchStack();
        {
            QMutexLocker lock(&g_gcMutex);
            if (g_pendingMismatch.isEmpty())
                g_pendingMismatch = msg + QStringLiteral(" detectionStack=") + stack;
        }
        if (g_previousHandler) {
            g_previousHandler(type, context, msg);
            if (!stack.isEmpty())
                g_previousHandler(QtCriticalMsg, context,
                    QStringLiteral("[Memory][QmlGc] DETECTION_STACK %1").arg(stack));
        }
        scheduleGcMismatchDelivery();
        return;
    }
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    if (type == QtWarningMsg
        && !BleCapability::linuxMissing()
        && msg.contains(QStringLiteral("Missing CAP_NET_ADMIN permission"))) {
        return;  // our independent probe says caps are effective; drop the false alarm
    }
#endif
    // QTBUG-146779: Qt spuriously emits "window doesn't exist." during early
    // startup and window/accessibility teardown. Harmless, but it lands at
    // critical severity and floods the error log. Drop until the upstream fix.
    if (msg == QLatin1String("window doesn't exist."))
        return;

    if (type == QtDebugMsg && BtLogFilter::isGcCrossValidationMessage(msg)) {
        recordGcMiss(msg);
        const auto line = BtLogFilter::gcCrossValidationLine(msg, ++g_gcCrossValidationMessages);
        if (line && g_previousHandler)
            g_previousHandler(QtWarningMsg, context, *line);
        return;
    }

    if (g_previousHandler) g_previousHandler(type, context, msg);
}
} // namespace

namespace BtLogFilter {

void install()
{
    if (g_previousHandler) return;
    g_previousHandler = qInstallMessageHandler(messageHandler);
}

void enableGcCrossValidation()
{
    if (!qEnvironmentVariableIsSet("QV4_MM_CROSS_VALIDATE_INCREMENTAL_GC"))
        qputenv("QV4_MM_CROSS_VALIDATE_INCREMENTAL_GC", "1");
}

void setGcPageStack(const QString& pages)
{
    QMutexLocker lock(&g_gcMutex);
    g_gcPages = pages.left(512);
}

void setGcMismatchHandler(std::function<void(const QString&)> handler)
{
    {
        QMutexLocker lock(&g_gcMutex);
        g_mismatchHandler = std::move(handler);
    }
    scheduleGcMismatchDelivery();
}

} // namespace BtLogFilter
