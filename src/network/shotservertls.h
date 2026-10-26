#pragma once

#include <QHostAddress>
#include <QNetworkInterface>
#include <QRegularExpression>
#include <QSet>
#include <QSocketNotifier>
#include <QSslCertificate>
#include <QSslServer>
#include <QTcpSocket>
#include <QTimer>
#include <QVariant>
#include <functional>
#include <utility>

#ifdef Q_OS_WIN
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace ShotServerTls {
// Set on a connection handed over unencrypted for /mcp; the server serves nothing else on it.
inline constexpr char kPlaintextMcpProperty[] = "plaintextMcpOnly";

inline QList<QHostAddress> localAddresses()
{
    QList<QHostAddress> addresses{QHostAddress(QHostAddress::LocalHost)};
    for (const auto& address : QNetworkInterface::allAddresses())
        if (!address.isLoopback() && address.protocol() == QAbstractSocket::IPv4Protocol
            && !addresses.contains(address))
            addresses.append(address);
    return addresses;
}

inline bool coversAddresses(const QMultiMap<QSsl::AlternativeNameEntryType, QString>& names,
                            const QList<QHostAddress>& addresses)
{
    if (!names.values(QSsl::DnsEntry).contains(QStringLiteral("localhost"))) return false;
    const auto ips = names.values(QSsl::IpAddressEntry);
    for (const auto& address : addresses)
        if (!ips.contains(address.toString())) return false;
    return true;
}

inline QStringList certificateNames(const QList<QHostAddress>& addresses)
{
    QStringList names{QStringLiteral("DNS:localhost")};
    for (const auto& address : addresses) names.append(QStringLiteral("IP:") + address.toString());
    return names;
}
}

class HttpRedirectSslServer : public QSslServer {
public:
    explicit HttpRedirectSslServer(std::function<bool(QSslServer*)> prepareTls = {}, QObject* parent = nullptr)
        : QSslServer(parent), m_prepareTls(std::move(prepareTls)) {}
    ~HttpRedirectSslServer() override { for (auto fd : std::as_const(m_pending)) closeFd(fd); }

protected:
    void incomingConnection(qintptr fd) override
    {
        auto* notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
        m_pending.insert(fd);
        auto* timeout = new QTimer(notifier);
        timeout->setSingleShot(true);
        timeout->start(5000);
        connect(timeout, &QTimer::timeout, this, [this, notifier, fd] {
            m_pending.remove(fd);
            closeFd(fd);
            notifier->setEnabled(false);
            notifier->deleteLater();
        });
        connect(notifier, &QSocketNotifier::activated, this, [this, notifier, timeout, fd] {
            unsigned char first = 0;
            const auto count = ::recv(nativeFd(fd), reinterpret_cast<char*>(&first), 1, MSG_PEEK);
            notifier->setEnabled(false);
            m_pending.remove(fd);
            timeout->stop();
            notifier->deleteLater();
            if (count <= 0) { closeFd(fd); return; }
            if (first == 0x16) {
                if (m_prepareTls && !m_prepareTls(this)) { closeFd(fd); return; }
                QSslServer::incomingConnection(fd);
                return;
            }
            readHttpRequest(fd);
        });
    }

private:
    std::function<bool(QSslServer*)> m_prepareTls;
    QSet<qintptr> m_pending;
#ifdef Q_OS_WIN
    static SOCKET nativeFd(qintptr fd) { return static_cast<SOCKET>(fd); }
#else
    static int nativeFd(qintptr fd) { return static_cast<int>(fd); }
#endif
    static void closeFd(qintptr fd)
    {
#ifdef Q_OS_WIN
        ::closesocket(nativeFd(fd));
#else
        ::close(nativeFd(fd));
#endif
    }

    void readHttpRequest(qintptr fd)
    {
        auto* socket = new QTcpSocket(this);
        if (!socket->setSocketDescriptor(fd)) { socket->deleteLater(); return; }
        constexpr qint64 maximumHeader = 16 * 1024;
        socket->setReadBufferSize(maximumHeader + 1);
        auto* timeout = new QTimer(socket);
        timeout->setSingleShot(true);
        timeout->start(5000);
        connect(timeout, &QTimer::timeout, socket, &QTcpSocket::disconnectFromHost);
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        const auto consume = [this, socket, timeout] {
            if (socket->property("redirectSent").toBool()) { socket->readAll(); return; }
            const auto request = socket->peek(socket->bytesAvailable());
            const auto end = request.indexOf("\r\n\r\n");
            if (end < 0) {
                if (request.size() > maximumHeader) socket->disconnectFromHost();
                return;
            }
            const auto firstLine = request.left(request.indexOf("\r\n")).split(' ');
            QString host;
            for (const auto& line : request.left(end).split('\n'))
                if (line.startsWith("Host:") || line.left(5).toLower() == "host:")
                    host = QString::fromLatin1(line.mid(5).trimmed());
            if (host.startsWith('[')) host = host.left(host.indexOf(']') + 1);
            else if (host.contains(':')) host = host.left(host.lastIndexOf(':'));
            static const QRegularExpression validHost(QStringLiteral("^[a-zA-Z0-9.\\-]+$|^\\[[0-9a-fA-F:]+\\]$"));
            if (end + 4 > maximumHeader || firstLine.size() != 3 || !firstLine.at(1).startsWith('/')
                || firstLine.at(1).contains('\r') || firstLine.at(1).contains('\n')
                || !validHost.match(host).hasMatch()) { socket->disconnectFromHost(); return; }
            const QByteArray& target = firstLine.at(1);
            if (target == "/mcp" || target.startsWith("/mcp?")) {
                serveInPlaintext(socket, timeout);
                return;
            }
            const QByteArray location = "https://" + host.toLatin1() + ':'
                + QByteArray::number(serverPort()) + firstLine.at(1);
            socket->setProperty("redirectSent", true);
            socket->readAll();
            socket->write("HTTP/1.1 307 Temporary Redirect\r\nLocation: " + location
                          + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            // Drain late request-body bytes until the client closes or the deadline expires.
        };
        connect(socket, &QTcpSocket::readyRead, socket, consume);
        consume();
    }

    // MCP clients verify certificates, so a 307 to the self-signed HTTPS port fails
    // them outright. The JSON-RPC endpoint alone is served as plain HTTP; browser
    // pages, /mcp/setup included, still redirect.
    void serveInPlaintext(QTcpSocket* socket, QTimer* timeout)
    {
        delete timeout;
        disconnect(socket, &QTcpSocket::readyRead, socket, nullptr);
        disconnect(socket, &QTcpSocket::disconnected, socket, nullptr);
        socket->setReadBufferSize(0);
        socket->setProperty(ShotServerTls::kPlaintextMcpProperty, true);
        addPendingConnection(socket);
        // The request was only peeked, so no further readyRead will announce it.
        QMetaObject::invokeMethod(socket, [socket] { emit socket->readyRead(); },
                                  Qt::QueuedConnection);
    }
};
