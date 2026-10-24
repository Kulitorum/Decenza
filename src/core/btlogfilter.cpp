#include "btlogfilter.h"

#include "../ble/blecapability.h"

#include <QString>

#include <atomic>

namespace {
QtMessageHandler g_previousHandler = nullptr;
std::atomic<int> g_gcCrossValidationMessages{0};

void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
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

} // namespace BtLogFilter
