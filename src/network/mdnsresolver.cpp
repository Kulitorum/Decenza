#include "mdnsresolver.h"

#ifdef Q_OS_ANDROID

#include <QHostAddress>
#include <QElapsedTimer>
#include <QByteArray>

// mjansson/mdns is header-only: MDNS_IMPLEMENTATION must be defined in exactly
// ONE translation unit across the whole binary. This file is that home — do
// NOT define it anywhere else (mqttclient.cpp used to, and now calls into here
// instead).
#define MDNS_IMPLEMENTATION
#include <mdns.h>

#include <cstring>
#include <cerrno>

namespace {

struct MdnsResolveContext {
    QByteArray hostname;   // lowercased, no trailing dot
    QString resolvedIp;
};

// mjansson/mdns record callback. Fires once per record in each response packet.
// We only care about A-record ANSWERS whose name matches the queried hostname.
int mdnsResolveCallback(int sock, const struct sockaddr* from, size_t addrlen,
                        mdns_entry_type_t entry, uint16_t query_id,
                        uint16_t rtype, uint16_t rclass, uint32_t ttl,
                        const void* data, size_t size,
                        size_t name_offset, size_t name_length,
                        size_t record_offset, size_t record_length,
                        void* user_data)
{
    Q_UNUSED(sock); Q_UNUSED(from); Q_UNUSED(addrlen);
    Q_UNUSED(query_id); Q_UNUSED(rclass); Q_UNUSED(ttl);
    Q_UNUSED(name_length);

    auto* ctx = static_cast<MdnsResolveContext*>(user_data);
    if (!ctx->resolvedIp.isEmpty())
        return 0;  // already found

    if (entry != MDNS_ENTRYTYPE_ANSWER || rtype != MDNS_RECORDTYPE_A)
        return 0;

    // Extract record name (handles DNS compression pointers).
    char namebuf[256];
    size_t nameOffset = name_offset;
    mdns_string_t name = mdns_string_extract(data, size, &nameOffset, namebuf, sizeof(namebuf));

    QByteArray recordName = QByteArray(name.str, static_cast<qsizetype>(name.length));
    if (recordName.endsWith('.'))
        recordName.chop(1);

    if (recordName.toLower() != ctx->hostname.toLower())
        return 0;

    struct sockaddr_in addr;
    mdns_record_parse_a(data, size, record_offset, record_length, &addr);
    ctx->resolvedIp = QHostAddress(ntohl(addr.sin_addr.s_addr)).toString();
    return 0;
}

}  // namespace

namespace MdnsResolver {

QString resolveHostname(const QString& hostname, int timeoutMs)
{
    // Open mDNS socket (binds to ephemeral port, joins the multicast group).
    struct sockaddr_in bindAddr;
    memset(&bindAddr, 0, sizeof(bindAddr));
    bindAddr.sin_family = AF_INET;
    bindAddr.sin_addr.s_addr = INADDR_ANY;
    int sock = mdns_socket_open_ipv4(&bindAddr);
    if (sock < 0)
        return {};

    char buffer[2048];
    QByteArray hostBytes = hostname.toUtf8();
    if (mdns_query_send(sock, MDNS_RECORDTYPE_A, hostBytes.constData(),
                        static_cast<size_t>(hostBytes.size()),
                        buffer, sizeof(buffer), 0) < 0) {
        mdns_socket_close(sock);
        return {};
    }

    MdnsResolveContext ctx;
    ctx.hostname = hostBytes;
    if (ctx.hostname.endsWith('.'))
        ctx.hostname.chop(1);

    QElapsedTimer deadline;
    deadline.start();

    while (deadline.elapsed() < timeoutMs && ctx.resolvedIp.isEmpty()) {
        int remaining = static_cast<int>(timeoutMs - deadline.elapsed());
        if (remaining <= 0) break;

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        struct timeval tv;
        tv.tv_sec = remaining / 1000;
        tv.tv_usec = (remaining % 1000) * 1000;

        int ret = select(sock + 1, &readfds, nullptr, nullptr, &tv);
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ret == 0) break;  // timeout slice elapsed with no data

        mdns_query_recv(sock, buffer, sizeof(buffer),
                        mdnsResolveCallback, &ctx, 0);
    }

    mdns_socket_close(sock);
    return ctx.resolvedIp;
}

}  // namespace MdnsResolver

#else  // !Q_OS_ANDROID

namespace MdnsResolver {
// Non-Android platforms resolve ".local" via QHostInfo (OS mDNS resolver), so
// this stub is never reached — present only so the TU links on all platforms.
QString resolveHostname(const QString& /*hostname*/, int /*timeoutMs*/) { return {}; }
}  // namespace MdnsResolver

#endif  // Q_OS_ANDROID
