#include "btlogfilter.h"

#include "../ble/blecapability.h"

#include <QString>

namespace {
QtMessageHandler g_previousHandler = nullptr;

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
    if (g_previousHandler) g_previousHandler(type, context, msg);
}
} // namespace

namespace BtLogFilter {

void install()
{
    if (g_previousHandler) return;
    g_previousHandler = qInstallMessageHandler(messageHandler);
}

} // namespace BtLogFilter
