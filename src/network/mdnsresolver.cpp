#include "mdnsresolver.h"

#ifdef Q_OS_ANDROID

#include <QHostAddress>
#include <QElapsedTimer>
#include <QByteArray>
#include <QDebug>

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
    // Diagnostics: distinguish "no packets arrived" from "packets arrived but
    // the wanted A record never did / was filtered". See resolveHostname().
    int recordsSeen = 0;     // every record across every response packet
    int aRecordsSeen = 0;    // A records specifically (any name)
    bool verbose = false;    // log every record when true
};

// Human-readable record-type label for the verbose log.
const char* rtypeName(uint16_t rtype)
{
    switch (rtype) {
        case MDNS_RECORDTYPE_A:    return "A";
        case MDNS_RECORDTYPE_PTR:  return "PTR";
        case MDNS_RECORDTYPE_SRV:  return "SRV";
        case MDNS_RECORDTYPE_AAAA: return "AAAA";
        case MDNS_RECORDTYPE_TXT:  return "TXT";
        default:                   return "?";
    }
}

// mjansson/mdns record callback. Fires once per record in each response packet,
// for records from ALL responders on the network (not just our target). We
// match A-record entries whose name equals the queried hostname.
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
    ctx->recordsSeen++;
    if (rtype == MDNS_RECORDTYPE_A)
        ctx->aRecordsSeen++;

    if (!ctx->resolvedIp.isEmpty())
        return 0;  // already found — keep counting but stop parsing

    // Extract record name (handles DNS compression pointers). Done for all
    // record types so the verbose log can show what's actually on the wire.
    char namebuf[256];
    size_t nameOffset = name_offset;
    mdns_string_t name = mdns_string_extract(data, size, &nameOffset, namebuf, sizeof(namebuf));
    QByteArray recordName = QByteArray(name.str, static_cast<qsizetype>(name.length));
    if (recordName.endsWith('.'))
        recordName.chop(1);

    if (ctx->verbose) {
        const char* entryStr = entry == MDNS_ENTRYTYPE_ANSWER ? "ANSWER"
                             : entry == MDNS_ENTRYTYPE_AUTHORITY ? "AUTH"
                             : entry == MDNS_ENTRYTYPE_ADDITIONAL ? "ADD'L" : "?";
        qDebug().noquote() << "[MdnsResolver]   rx" << entryStr
                           << rtypeName(rtype) << "name=" << recordName;
    }

    if (rtype != MDNS_RECORDTYPE_A)
        return 0;

    // Accept the A record from ANY section (ANSWER/ADDITIONAL/AUTHORITY) — some
    // lightweight responders (e.g. ESP32) place the host A record outside the
    // ANSWER section. Match is by name, which is the real correctness gate.
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
    if (sock < 0) {
        qWarning() << "[MdnsResolver] socket open FAILED for" << hostname
                   << "errno=" << errno;
        return {};
    }

    char buffer[2048];
    QByteArray hostBytes = hostname.toUtf8();

    MdnsResolveContext ctx;
    ctx.hostname = hostBytes;
    if (ctx.hostname.endsWith('.'))
        ctx.hostname.chop(1);
    ctx.verbose = true;  // diagnostic: log every record seen during the probe

    qDebug().noquote() << "[MdnsResolver] start host=" << hostname
                       << "timeout=" << timeoutMs << "ms sock=" << sock;

    int sendCount = 0;

    // mDNS clients MUST retransmit: a single multicast query can be silently
    // dropped (WiFi multicast is unacknowledged and sent at a low rate), and a
    // busy responder may miss it entirely. This matters acutely for the Half
    // Decent Scale — its ESP32 shares one radio between BLE and WiFi, so while
    // it's BLE-connected (heartbeats every ~2 s) incoming multicast is often
    // missed. A single query frequently goes unanswered even though the scale
    // resolves fine for `dns-sd`/Bonjour, which re-ask. So we re-send the query
    // every kRetransmitMs until we get an answer or the deadline passes.
    constexpr int kRetransmitMs = 750;

    QElapsedTimer deadline;
    deadline.start();
    qint64 nextSendAt = 0;  // due immediately, then every kRetransmitMs

    while (deadline.elapsed() < timeoutMs && ctx.resolvedIp.isEmpty()) {
        if (deadline.elapsed() >= nextSendAt) {
            // Best-effort: a failed send is not fatal (transient ENOBUFS on a
            // congested interface) — keep polling and retry on the next tick.
            int sendRet = mdns_query_send(sock, MDNS_RECORDTYPE_A, hostBytes.constData(),
                                          static_cast<size_t>(hostBytes.size()),
                                          buffer, sizeof(buffer), 0);
            ++sendCount;
            qDebug().noquote() << "[MdnsResolver]   query #" << sendCount
                               << "sent ret=" << sendRet
                               << (sendRet < 0 ? QString(" errno=%1").arg(errno) : QString());
            nextSendAt = deadline.elapsed() + kRetransmitMs;
        }

        // Wake at least every kRetransmitMs so we can retransmit, but never
        // sleep past the overall deadline.
        const int remaining = static_cast<int>(timeoutMs - deadline.elapsed());
        if (remaining <= 0) break;
        const int slice = qMin(remaining, kRetransmitMs);

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        struct timeval tv;
        tv.tv_sec = slice / 1000;
        tv.tv_usec = (slice % 1000) * 1000;

        int ret = select(sock + 1, &readfds, nullptr, nullptr, &tv);
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ret == 0) continue;  // slice elapsed with no data — retransmit

        mdns_query_recv(sock, buffer, sizeof(buffer),
                        mdnsResolveCallback, &ctx, 0);
    }

    mdns_socket_close(sock);

    // Summary fork for diagnosis:
    //  - records=0           → NO multicast responses reached our socket at all
    //                          (interface/multicast-lock/routing problem).
    //  - records>0, aRecs=0  → responses arrive but no A records (unexpected).
    //  - records>0, aRecs>0, result empty → A records arrive but none named
    //                          like our host → the scale isn't answering THIS
    //                          query (responder/name issue), even though other
    //                          hosts on the LAN are.
    //  - result set          → success.
    qDebug().noquote() << "[MdnsResolver] done host=" << hostname
                       << "result=" << (ctx.resolvedIp.isEmpty() ? QString("(none)") : ctx.resolvedIp)
                       << "queries=" << sendCount
                       << "records=" << ctx.recordsSeen
                       << "aRecords=" << ctx.aRecordsSeen
                       << "elapsed=" << deadline.elapsed() << "ms";

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
