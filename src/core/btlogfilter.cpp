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
    if (g_previousHandler) g_previousHandler(type, context, msg);
}
} // namespace

namespace BtLogFilter {

void install()
{
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    if (g_previousHandler) return;
    g_previousHandler = qInstallMessageHandler(messageHandler);
#endif
}

} // namespace BtLogFilter
