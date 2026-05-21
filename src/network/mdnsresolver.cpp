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
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

namespace {

struct MdnsResolveContext {
    QByteArray hostname;   // no trailing dot
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
// match A-record entries whose name equals the queried hostname. When bound to
// port 5353 we see every mDNS packet on the LAN, so this is called a lot — the
// name match is what isolates our answer.
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

// Build a Multicast DNS query for the host's A record with the QU
// (unicast-response-requested) bit CLEARED, so the responder answers via
// MULTICAST rather than unicast. This is the crux of the Android fix:
//
//   * mjansson's mdns_query_send() always sets the QU bit (it hardcodes
//     class = MDNS_UNICAST_RESPONSE | MDNS_CLASS_IN). Combined with our socket
//     being bound to port 5353, a QU query makes the responder send a *unicast*
//     reply to <our-ip>:5353. On Android the system mDNS daemon ALSO holds
//     port 5353, and the kernel's SO_REUSEPORT load-balancer hands each
//     *unicast* datagram to exactly ONE of the sharing sockets — in practice
//     the daemon, never us. Result: records=0 (this is what the earlier
//     port-5353 attempt hit).
//   * A QM query (QU bit clear) sent from source port 5353 makes the responder
//     MULTICAST its reply to 224.0.0.251:5353. Multicast is delivered to ALL
//     sockets that joined the group on that port, so our socket gets a copy
//     alongside the daemon. Verified on-wire against both the HDS (ESP-IDF
//     mDNS) and a Linux/avahi host: ephemeral/QU -> unicast reply; 5353/QM ->
//     multicast reply.
//
// The packet is a standard DNS query: 12-byte header (txid 0, one question) +
// QNAME (length-prefixed labels) + QTYPE=A + QCLASS=IN.
QByteArray buildQueryQM(const QByteArray& host)
{
    QByteArray pkt;
    auto put16 = [&pkt](uint16_t v) {
        pkt.append(static_cast<char>((v >> 8) & 0xFF));
        pkt.append(static_cast<char>(v & 0xFF));
    };
    put16(0);  // transaction id (0 for mDNS)
    put16(0);  // flags (standard query)
    put16(1);  // QDCOUNT
    put16(0);  // ANCOUNT
    put16(0);  // NSCOUNT
    put16(0);  // ARCOUNT
    for (const QByteArray& label : host.split('.')) {
        if (label.isEmpty()) continue;  // skip empties (e.g. a trailing dot)
        pkt.append(static_cast<char>(label.size() & 0xFF));
        pkt.append(label);
    }
    pkt.append('\0');  // root label terminates QNAME
    put16(1);  // QTYPE  = A
    put16(1);  // QCLASS = IN  (QU bit 0x8000 cleared => multicast reply)
    return pkt;
}

}  // namespace

namespace MdnsResolver {

QString resolveHostname(const QString& hostname, int timeoutMs)
{
    // Bind the query socket to port 5353 (with SO_REUSEADDR/SO_REUSEPORT, set by
    // mdns_socket_open_ipv4). The source port MUST be 5353 for two reasons:
    //   1. A query whose source port is 5353 is a real mDNS query, which
    //      responders answer via MULTICAST. A query from an ephemeral port is
    //      treated as a legacy unicast query (RFC 6762 §6.7) and answered via
    //      UNICAST — which Android does not reliably deliver to an app socket
    //      (observed: ephemeral-port queries are sent fine but the reply never
    //      arrives, records=0).
    //   2. mdns_socket_open_ipv4 joins 224.0.0.251 on the bound port, so a
    //      socket bound to 5353 receives the multicast reply alongside the OS
    //      mDNS daemon.
    // We also send QM (not QU) queries — see buildQueryQM() for why clearing the
    // QU bit is required even when querying from 5353.
    struct sockaddr_in bindAddr;
    memset(&bindAddr, 0, sizeof(bindAddr));
    bindAddr.sin_family = AF_INET;
    bindAddr.sin_addr.s_addr = INADDR_ANY;
    bindAddr.sin_port = htons(5353);
    int sock = mdns_socket_open_ipv4(&bindAddr);
    if (sock < 0) {
        // Very unlikely with SO_REUSEPORT, but if the 5353 bind is refused fall
        // back to an ephemeral port so we at least send the query. (The reply
        // would then be unicast and likely missed — degraded, not a real fix.)
        qWarning() << "[MdnsResolver] 5353 bind failed for" << hostname
                   << "errno=" << errno << "- retrying on ephemeral port";
        bindAddr.sin_port = 0;
        sock = mdns_socket_open_ipv4(&bindAddr);
    }
    if (sock < 0) {
        qWarning() << "[MdnsResolver] socket open FAILED for" << hostname
                   << "errno=" << errno;
        return {};
    }

    char buffer[2048];

    MdnsResolveContext ctx;
    ctx.hostname = hostname.toUtf8();
    if (ctx.hostname.endsWith('.'))
        ctx.hostname.chop(1);
    ctx.verbose = true;  // diagnostic: log every record seen during the probe

    // mDNS multicast destination: 224.0.0.251:5353.
    struct sockaddr_in mcast;
    memset(&mcast, 0, sizeof(mcast));
    mcast.sin_family = AF_INET;
    mcast.sin_addr.s_addr = htonl((static_cast<uint32_t>(224) << 24) | 251U);
    mcast.sin_port = htons(5353);

    const QByteArray queryPacket = buildQueryQM(ctx.hostname);

    qDebug().noquote() << "[MdnsResolver] start host=" << hostname
                       << "timeout=" << timeoutMs << "ms sock=" << sock
                       << "(port 5353, QM)";

    int sendCount = 0;

    // mDNS clients MUST retransmit: a single multicast query can be silently
    // dropped (WiFi multicast is unacknowledged and sent at a low rate), and a
    // busy responder may miss it entirely. This matters acutely for the Half
    // Decent Scale — its ESP32 shares one radio between BLE and WiFi, so while
    // it's BLE-connected (heartbeats every ~2 s) incoming multicast is often
    // missed. So we re-send the query every kRetransmitMs until we get an answer
    // or the deadline passes.
    constexpr int kRetransmitMs = 750;

    QElapsedTimer deadline;
    deadline.start();
    qint64 nextSendAt = 0;  // due immediately, then every kRetransmitMs

    while (deadline.elapsed() < timeoutMs && ctx.resolvedIp.isEmpty()) {
        if (deadline.elapsed() >= nextSendAt) {
            // Best-effort: a failed send is not fatal (transient ENOBUFS on a
            // congested interface) — keep polling and retry on the next tick.
            const ssize_t sret = sendto(sock, queryPacket.constData(),
                                        static_cast<size_t>(queryPacket.size()), 0,
                                        reinterpret_cast<struct sockaddr*>(&mcast),
                                        sizeof(mcast));
            ++sendCount;
            qDebug().noquote() << "[MdnsResolver]   query #" << sendCount
                               << "sent ret=" << static_cast<int>(sret)
                               << (sret < 0 ? QString(" errno=%1").arg(errno) : QString());
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

        // Receive one packet. Bound to 5353 we see all LAN mDNS traffic, but
        // when packets are queued select() returns immediately on the next pass
        // (the retransmit timer only gates sends), so the outer loop drains the
        // queue at full speed.
        mdns_query_recv(sock, buffer, sizeof(buffer),
                        mdnsResolveCallback, &ctx, 0);
    }

    mdns_socket_close(sock);

    // Summary fork for diagnosis:
    //  - records=0           → NO mDNS packets reached our socket at all
    //                          (multicast-lock / interface / routing problem,
    //                          since bound to 5353 we should see LAN-wide mDNS).
    //  - records>0, aRecs=0  → packets arrive but no A records (unexpected).
    //  - records>0, aRecs>0, result empty → A records arrive but none named like
    //                          our host → the scale isn't answering THIS query.
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
