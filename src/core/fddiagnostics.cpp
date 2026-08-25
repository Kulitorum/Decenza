#include "fddiagnostics.h"

#if defined(Q_OS_LINUX) || defined(Q_OS_ANDROID)

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>

namespace {

struct SocketDetails {
    QString family;
    QString protocol;
    QString state;
    QString localAddress;
    int localPort = -1;
    QString remoteAddress;
    int remotePort = -1;
    QString path;
};

QString tcpStateName(const QString& state)
{
    static const QHash<QString, QString> states{
        {QStringLiteral("01"), QStringLiteral("ESTABLISHED")},
        {QStringLiteral("02"), QStringLiteral("SYN_SENT")},
        {QStringLiteral("03"), QStringLiteral("SYN_RECV")},
        {QStringLiteral("04"), QStringLiteral("FIN_WAIT1")},
        {QStringLiteral("05"), QStringLiteral("FIN_WAIT2")},
        {QStringLiteral("06"), QStringLiteral("TIME_WAIT")},
        {QStringLiteral("07"), QStringLiteral("CLOSE")},
        {QStringLiteral("08"), QStringLiteral("CLOSE_WAIT")},
        {QStringLiteral("09"), QStringLiteral("LAST_ACK")},
        {QStringLiteral("0A"), QStringLiteral("LISTEN")},
        {QStringLiteral("0B"), QStringLiteral("CLOSING")},
    };
    return states.value(state.toUpper(), QStringLiteral("UNKNOWN(%1)").arg(state));
}

QString unixTypeName(const QString& type)
{
    static const QHash<QString, QString> types{
        {QStringLiteral("0001"), QStringLiteral("stream")},
        {QStringLiteral("0002"), QStringLiteral("datagram")},
        {QStringLiteral("0005"), QStringLiteral("seqpacket")},
    };
    return types.value(type.toUpper(), QStringLiteral("unknown(%1)").arg(type));
}

QString ipv4Address(const QString& encoded)
{
    bool ok = false;
    const quint32 value = encoded.toUInt(&ok, 16);
    if (!ok)
        return encoded;
    return QStringLiteral("%1.%2.%3.%4")
        .arg(value & 0xff)
        .arg((value >> 8) & 0xff)
        .arg((value >> 16) & 0xff)
        .arg((value >> 24) & 0xff);
}

// IPv6 addresses in /proc/net/tcp6 are four native-endian 32-bit words.
// Reverse each word into network-byte order before formatting colon notation.
QString ipv6Address(const QString& encoded)
{
    if (encoded.size() != 32)
        return encoded;

    QByteArray bytes;
    bytes.reserve(16);
    for (int word = 0; word < 4; ++word) {
        bool ok = false;
        const quint32 value = encoded.mid(word * 8, 8).toUInt(&ok, 16);
        if (!ok)
            return encoded;
        for (int byte = 0; byte < 4; ++byte)
            bytes.append(static_cast<char>((value >> (byte * 8)) & 0xff));
    }

    QStringList groups;
    groups.reserve(8);
    for (int i = 0; i < bytes.size(); i += 2) {
        const quint16 group = (static_cast<quint8>(bytes[i]) << 8)
            | static_cast<quint8>(bytes[i + 1]);
        groups.append(QString::number(group, 16));
    }
    return groups.join(QLatin1Char(':'));
}

bool parseEndpoint(const QString& encoded, bool ipv6, QString* address, int* port)
{
    const qsizetype separator = encoded.indexOf(QLatin1Char(':'));
    if (separator <= 0 || separator == encoded.size() - 1)
        return false;

    bool ok = false;
    const uint parsedPort = encoded.mid(separator + 1).toUInt(&ok, 16);
    if (!ok || parsedPort > 65535)
        return false;

    *address = ipv6 ? ipv6Address(encoded.left(separator)) : ipv4Address(encoded.left(separator));
    *port = static_cast<int>(parsedPort);
    return true;
}

void readInetSockets(const QString& fileName, const QString& protocol, bool ipv6,
                     QHash<qulonglong, SocketDetails>* sockets)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    file.readLine(); // column header
    while (!file.atEnd()) {
        const QStringList fields = QString::fromUtf8(file.readLine()).simplified()
            .split(QLatin1Char(' '), Qt::SkipEmptyParts);
        // sl, local, remote, state, queues, timer, retransmit, uid, timeout, inode
        if (fields.size() < 10)
            continue;

        bool inodeOk = false;
        const qulonglong inode = fields.at(9).toULongLong(&inodeOk);
        if (!inodeOk)
            continue;

        SocketDetails details;
        details.family = ipv6 ? QStringLiteral("inet6") : QStringLiteral("inet4");
        details.protocol = protocol;
        details.state = protocol.startsWith(QStringLiteral("tcp"))
            ? tcpStateName(fields.at(3)) : fields.at(3).toUpper();
        if (!parseEndpoint(fields.at(1), ipv6, &details.localAddress, &details.localPort)
            || !parseEndpoint(fields.at(2), ipv6, &details.remoteAddress, &details.remotePort))
            continue;
        sockets->insert(inode, details);
    }
}

void readUnixSockets(QHash<qulonglong, SocketDetails>* sockets)
{
    QFile file(QStringLiteral("/proc/self/net/unix"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    file.readLine(); // column header
    while (!file.atEnd()) {
        const QStringList fields = QString::fromUtf8(file.readLine()).simplified()
            .split(QLatin1Char(' '), Qt::SkipEmptyParts);
        // address, refcount, protocol, flags, type, state, inode, [path]
        if (fields.size() < 7)
            continue;

        bool inodeOk = false;
        const qulonglong inode = fields.at(6).toULongLong(&inodeOk);
        if (!inodeOk)
            continue;

        SocketDetails details;
        details.family = QStringLiteral("unix");
        details.protocol = unixTypeName(fields.at(4));
        details.state = fields.at(5).toUpper();
        if (fields.size() > 7)
            details.path = fields.mid(7).join(QLatin1Char(' '));
        sockets->insert(inode, details);
    }
}

QHash<qulonglong, SocketDetails> socketDetailsByInode()
{
    QHash<qulonglong, SocketDetails> sockets;
    readInetSockets(QStringLiteral("/proc/self/net/tcp"), QStringLiteral("tcp"), false, &sockets);
    readInetSockets(QStringLiteral("/proc/self/net/tcp6"), QStringLiteral("tcp6"), true, &sockets);
    readInetSockets(QStringLiteral("/proc/self/net/udp"), QStringLiteral("udp"), false, &sockets);
    readInetSockets(QStringLiteral("/proc/self/net/udp6"), QStringLiteral("udp6"), true, &sockets);
    readUnixSockets(&sockets);
    return sockets;
}

QString descriptorKind(const QString& target)
{
    if (target.startsWith(QStringLiteral("socket:[")))
        return QStringLiteral("socket");
    if (target.startsWith(QStringLiteral("pipe:[")))
        return QStringLiteral("pipe");
    if (target.startsWith(QStringLiteral("anon_inode:")))
        return QStringLiteral("anon_inode");
    if (target.startsWith(QLatin1Char('/')))
        return QStringLiteral("file");
    return QStringLiteral("other");
}

bool socketInode(const QString& target, qulonglong* inode)
{
    static const QRegularExpression pattern(QStringLiteral("^socket:\\[(\\d+)\\]$"));
    const QRegularExpressionMatch match = pattern.match(target);
    if (!match.hasMatch())
        return false;
    bool ok = false;
    *inode = match.captured(1).toULongLong(&ok);
    return ok;
}

QJsonObject detailsToJson(const SocketDetails& details)
{
    QJsonObject result{{"family", details.family},
                       {"protocol", details.protocol},
                       {"state", details.state}};
    if (!details.localAddress.isEmpty()) {
        result["localAddress"] = details.localAddress;
        result["localPort"] = details.localPort;
        result["remoteAddress"] = details.remoteAddress;
        result["remotePort"] = details.remotePort;
    }
    if (!details.path.isEmpty())
        result["path"] = details.path;
    return result;
}

} // namespace

namespace FdDiagnostics {

QJsonObject snapshot()
{
    QDir fdDir(QStringLiteral("/proc/self/fd"));
    if (!fdDir.exists())
        return QJsonObject{{"supported", false},
                           {"error", QStringLiteral("/proc/self/fd is not available")}};

    QStringList entries = fdDir.entryList(QDir::AllEntries | QDir::Hidden | QDir::System
                                          | QDir::NoDotAndDotDot);
    std::sort(entries.begin(), entries.end(), [](const QString& a, const QString& b) {
        return a.toInt() < b.toInt();
    });

    const QHash<qulonglong, SocketDetails> sockets = socketDetailsByInode();
    QHash<QString, int> byKind;
    QHash<QString, int> socketProtocols;
    QHash<QString, int> socketStates;
    QJsonArray descriptors;
    int socketCount = 0;
    int mappedSocketCount = 0;

    for (const QString& entry : entries) {
        bool fdOk = false;
        const int fd = entry.toInt(&fdOk);
        if (!fdOk)
            continue;

        const QString target = QFileInfo(fdDir.filePath(entry)).symLinkTarget();
        const QString kind = descriptorKind(target);
        byKind[kind] += 1;

        QJsonObject descriptor{{"fd", fd}, {"kind", kind}, {"target", target}};
        qulonglong inode = 0;
        if (socketInode(target, &inode)) {
            ++socketCount;
            descriptor["inode"] = QString::number(inode);
            const auto socket = sockets.constFind(inode);
            if (socket != sockets.cend()) {
                ++mappedSocketCount;
                const QJsonObject socketJson = detailsToJson(socket.value());
                for (auto it = socketJson.begin(); it != socketJson.end(); ++it)
                    descriptor[it.key()] = it.value();
                socketProtocols[socket->protocol] += 1;
                socketStates[socket->state] += 1;
            } else {
                descriptor["family"] = QStringLiteral("unmapped");
            }
        }
        descriptors.append(descriptor);
    }

    QJsonObject kinds;
    for (auto it = byKind.cbegin(); it != byKind.cend(); ++it)
        kinds[it.key()] = it.value();
    QJsonObject protocols;
    for (auto it = socketProtocols.cbegin(); it != socketProtocols.cend(); ++it)
        protocols[it.key()] = it.value();
    QJsonObject states;
    for (auto it = socketStates.cbegin(); it != socketStates.cend(); ++it)
        states[it.key()] = it.value();

    return QJsonObject{{"supported", true},
                       {"openFdCount", descriptors.size()},
                       {"descriptorKinds", kinds},
                       {"socketFdCount", socketCount},
                       {"mappedSocketCount", mappedSocketCount},
                       {"socketProtocols", protocols},
                       {"socketStates", states},
                       {"descriptors", descriptors}};
}

} // namespace FdDiagnostics

#else

namespace FdDiagnostics {

QJsonObject snapshot()
{
    return QJsonObject{{"supported", false},
                       {"error", QStringLiteral("Descriptor snapshots require Linux or Android procfs")}};
}

} // namespace FdDiagnostics

#endif
