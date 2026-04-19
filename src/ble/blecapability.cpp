#include "blecapability.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QThread>
#include <atomic>
#include <mutex>

namespace {
std::once_flag g_checkFlag;
bool g_missing = false;
QString g_setcapCommand;

void ensureChecked()
{
    std::call_once(g_checkFlag, [] {
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
            constexpr quint64 CAP_NET_ADMIN_BIT = quint64(1) << 12;
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
    });
}

#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
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

void collectAndLogDiagnostics()
{
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
        // QProcess::waitForFinished() blocks up to 2 s per subprocess; spawn
        // a worker thread so the BLE error handler (main thread) returns
        // immediately.
        QThread* t = QThread::create([] { collectAndLogDiagnostics(); });
        QObject::connect(t, &QThread::finished, t, &QObject::deleteLater);
        t->start();
    });
#endif
}

bool takeBluezCacheHintToken()
{
    static std::atomic_bool fired{false};
    return !fired.exchange(true);
}

} // namespace BleCapability
