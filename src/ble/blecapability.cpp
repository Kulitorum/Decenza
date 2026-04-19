#include "blecapability.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <atomic>
#include <mutex>

namespace {
// Cached result of the /proc/self/status CAP_NET_ADMIN probe. BlueZ needs
// CAP_NET_ADMIN to distinguish random from public BLE addresses; without it,
// connects to random-address peripherals (the DE1) fail with
// UnknownRemoteDeviceError. The capability is granted via
// `sudo setcap 'cap_net_admin+eip' <binary>` and is often cleared by OS
// package updates.
bool g_checked = false;
bool g_missing = false;
QString g_setcapCommand;

void ensureChecked()
{
    if (g_checked) return;
    g_checked = true;
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    QFile f(QStringLiteral("/proc/self/status"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    while (!f.atEnd()) {
        const QByteArray line = f.readLine();
        if (!line.startsWith("CapEff:")) continue;
        const QString hex = QString::fromUtf8(line.mid(7)).trimmed();
        bool ok = false;
        const quint64 capEff = hex.toULongLong(&ok, 16);
        if (!ok) return;
        constexpr quint64 CAP_NET_ADMIN_BIT = quint64(1) << 12;  // CAP_NET_ADMIN = 12
        if ((capEff & CAP_NET_ADMIN_BIT) == 0) {
            g_missing = true;
            g_setcapCommand =
                QStringLiteral("sudo setcap 'cap_net_admin+eip' %1")
                    .arg(QCoreApplication::applicationFilePath());
            qWarning().noquote() << "BleCapability: CAP_NET_ADMIN missing — BLE connects to random-address devices (including the DE1) will fail. Fix:"
                                 << g_setcapCommand;
        }
        return;
    }
#endif
}

#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
// Run a short-lived external command and return its stdout (trimmed).
// Returns an empty string if the binary isn't installed or the command
// exceeded the 2-second budget. Stays on the caller's thread — only
// invoked from the one-shot diagnostics dump, which runs off the BLE
// error handler path.
QString runBriefly(const QString& program, const QStringList& args)
{
    const QString resolved = QStandardPaths::findExecutable(program);
    if (resolved.isEmpty()) return QString();
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(resolved, args);
    if (!p.waitForFinished(2000)) {
        p.kill();
        p.waitForFinished(250);
        return QString();
    }
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

void logProcStatusCaps()
{
    QFile f(QStringLiteral("/proc/self/status"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    while (!f.atEnd()) {
        const QByteArray line = f.readLine();
        if (line.startsWith("CapEff:") || line.startsWith("CapBnd:")
            || line.startsWith("CapInh:") || line.startsWith("CapAmb:")) {
            qInfo().noquote() << "BtDiagnostics:" << QString::fromUtf8(line).trimmed();
        }
    }
}
#endif
} // namespace

namespace BleCapability {

bool linuxMissing()
{
    ensureChecked();
    return g_missing;
}

QString linuxSetcapCommand()
{
    ensureChecked();
    return g_setcapCommand;
}

void logLinuxBtDiagnosticsOnce()
{
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    static std::once_flag flag;
    std::call_once(flag, [] {
        qInfo().noquote() << "BtDiagnostics: ---- Linux BT diagnostics (one-shot) ----";
        qInfo().noquote() << "BtDiagnostics: binary =" << QCoreApplication::applicationFilePath();
        logProcStatusCaps();

        const QString getcap = runBriefly(QStringLiteral("getcap"),
                                          {QCoreApplication::applicationFilePath()});
        if (!getcap.isEmpty())
            qInfo().noquote() << "BtDiagnostics: getcap =" << getcap;

        const QString bluezVersion = runBriefly(QStringLiteral("bluetoothctl"),
                                                {QStringLiteral("--version")});
        if (!bluezVersion.isEmpty())
            qInfo().noquote() << "BtDiagnostics: bluetoothctl =" << bluezVersion;

        const QString hci = runBriefly(QStringLiteral("hciconfig"), {QStringLiteral("-a")});
        if (!hci.isEmpty()) {
            const auto lines = hci.split(u'\n');
            for (const QString& l : lines)
                qInfo().noquote() << "BtDiagnostics: hciconfig:" << l;
        }
        qInfo().noquote() << "BtDiagnostics: ---- end ----";
    });
#endif
}

bool takeBluezCacheHintToken()
{
    static std::atomic_bool fired{false};
    return !fired.exchange(true);
}

} // namespace BleCapability
