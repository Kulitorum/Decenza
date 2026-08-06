#include "mdnsresolver.h"

// Compiled on every platform EXCEPT iOS — keep in step with the `if(NOT IOS)`
// blocks in CMakeLists.txt that fetch the library and add its include directory.
// iOS is the only exclusion because a raw multicast socket there needs an
// entitlement Apple grants by application (see the header).
//
// macOS builds this deliberately even though it defaults to Bonjour: it is the
// only way to exercise the backend Android and Windows/Linux ship without
// deploying to a device. Do NOT narrow this to Q_OS_DARWIN / `NOT APPLE`.
//
// Wider than the original guard (Q_OS_ANDROID). It used to be true that
// resolveHostname() was only *called* on Android; it no longer is. The call site
// in WifiScaleDiscovery::probe() is now a runtime branch on
// useDirectHostnameResolver(), so a desktop build can be pointed at the exact
// A-record path Android ships — which is why this file has to compile
// everywhere. See HostnameResolver in the header.
namespace {
// Declared ahead of the iOS guard below because the accessors that read it are
// compiled on every platform, while the socket code that acts on it is not.
// Written by the MCP tool on the main thread, read by whichever worker thread
// opens the next query socket — same race, and same treatment, as g_backend.
std::atomic<MdnsResolver::QueryPort> g_queryPort{MdnsResolver::QueryPort::Auto};
}  // namespace

#ifndef Q_OS_IOS

#include "multicastlock.h"

#include <QHostAddress>
#include <QElapsedTimer>
#include <QByteArray>
#include <QDebug>
#include <QSet>

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

// ---------------------------------------------------------------------------
// DNS-SD service browse
// ---------------------------------------------------------------------------

// Accumulates one browse. Keyed by the FULL instance name as it appears on the
// wire ("Half Decent Scale._decentscale._tcp.local"), because that is what the
// SRV and TXT records are named after.
struct MdnsBrowseInstance {
    // Instance name as it appeared on the wire, ORIGINAL CASE. The map key is
    // lowercased so the PTR/SRV/TXT joins are case-insensitive (DNS names are),
    // but this is what the user sees as the scale's name — lowercasing it would
    // turn "Half Decent Scale" into "half decent scale" on every row.
    QByteArray displayName;
    QByteArray target;   // SRV target host, trailing dot stripped
    quint16 port = 0;
    QMap<QString, QString> txt;
};

struct MdnsBrowseContext {
    QByteArray serviceType;  // lowercased, no trailing dot
    QMap<QByteArray, MdnsBrowseInstance> instances;  // full instance name -> data
    QMap<QByteArray, QString> addresses;             // host -> dotted-quad IPv4
    QSet<QByteArray> reported;  // already handed to the incremental callback
    int recordsSeen = 0;
};

// Extract a record's name, lowercased with any trailing dot removed.
QByteArray recordNameOf(const void* data, size_t size, size_t name_offset)
{
    char namebuf[256];
    size_t off = name_offset;
    mdns_string_t name = mdns_string_extract(data, size, &off, namebuf, sizeof(namebuf));
    QByteArray out(name.str, static_cast<qsizetype>(name.length));
    if (out.endsWith('.'))
        out.chop(1);
    // Lowercase to match the contract above. DNS names are case-insensitive on
    // the wire, and the SRV/TXT/A joins below key off these strings — a
    // responder that answers with different casing in PTR rdata than in the SRV
    // owner name would silently fail to join, and the instance would then be
    // logged as a "dropped unresolved" stale registration rather than a bug.
    return out.toLower();
}

// "Half Decent Scale (hdstest)._decentscale._tcp.local" + "_decentscale._tcp.local"
// -> "Half Decent Scale (hdstest)".
//
// mjansson joins DNS labels with '.', copying label bytes verbatim, so spaces
// and parentheses in a DNS-SD instance label survive unescaped. A literal '.'
// inside an instance label would be ambiguous here; DNS-SD allows it but it is
// vanishingly rare and the fallback (returning the full name) is still usable.
QString stripServiceSuffix(const QByteArray& fullName, const QByteArray& serviceType)
{
    const QByteArray suffix = "." + serviceType;
    if (fullName.size() > suffix.size() && fullName.toLower().endsWith(suffix))
        return QString::fromUtf8(fullName.left(fullName.size() - suffix.size()));
    return QString::fromUtf8(fullName);
}

// Build the public result for one complete instance. Caller must already have
// checked that the SRV target and its address are present.
MdnsResolver::ServiceInstance makeInstance(const QByteArray& fullName,
                                           const MdnsBrowseInstance& inst,
                                           const MdnsBrowseContext& ctx)
{
    MdnsResolver::ServiceInstance si;
    // Strip the suffix from the ORIGINAL-CASE name, not the lowercased key.
    si.instanceName = stripServiceSuffix(
        inst.displayName.isEmpty() ? fullName : inst.displayName, ctx.serviceType);
    si.hostname = QString::fromUtf8(inst.target);
    si.address = ctx.addresses.value(inst.target);
    si.port = inst.port;
    si.txt = inst.txt;
    return si;
}

// Browse callback. Fires once per record in every response packet, from all
// responders — we sort them by type and name into the context.
int mdnsBrowseCallback(int sock, const struct sockaddr* from, size_t addrlen,
                       mdns_entry_type_t entry, uint16_t query_id,
                       uint16_t rtype, uint16_t rclass, uint32_t ttl,
                       const void* data, size_t size,
                       size_t name_offset, size_t name_length,
                       size_t record_offset, size_t record_length,
                       void* user_data)
{
    Q_UNUSED(sock); Q_UNUSED(from); Q_UNUSED(addrlen);
    Q_UNUSED(entry); Q_UNUSED(query_id); Q_UNUSED(rclass); Q_UNUSED(ttl);
    Q_UNUSED(name_length);

    auto* ctx = static_cast<MdnsBrowseContext*>(user_data);
    ctx->recordsSeen++;

    const QByteArray recordName = recordNameOf(data, size, name_offset);
    char strbuf[256];

    switch (rtype) {
        case MDNS_RECORDTYPE_PTR: {
            // Only PTRs for the service type we asked about; the network is
            // full of other services answering their own browses.
            if (recordName != ctx->serviceType)   // both already lowercased
                return 0;
            mdns_string_t inst = mdns_record_parse_ptr(data, size, record_offset,
                                                       record_length, strbuf, sizeof(strbuf));
            QByteArray instName(inst.str, static_cast<qsizetype>(inst.length));
            if (instName.endsWith('.'))
                instName.chop(1);
            const QByteArray display = instName;      // preserve wire casing
            instName = instName.toLower();             // key: matches recordNameOf()
            if (!instName.isEmpty() && !ctx->instances.contains(instName)) {
                MdnsBrowseInstance fresh;
                fresh.displayName = display;
                ctx->instances.insert(instName, fresh);
            }
            break;
        }
        case MDNS_RECORDTYPE_SRV: {
            if (!ctx->instances.contains(recordName))
                return 0;
            mdns_record_srv_t srv = mdns_record_parse_srv(data, size, record_offset,
                                                          record_length, strbuf, sizeof(strbuf));
            QByteArray target(srv.name.str, static_cast<qsizetype>(srv.name.length));
            if (target.endsWith('.'))
                target.chop(1);
            target = target.toLower();       // key into ctx->addresses
            MdnsBrowseInstance& inst = ctx->instances[recordName];
            inst.target = target;
            inst.port = srv.port;
            break;
        }
        case MDNS_RECORDTYPE_TXT: {
            if (!ctx->instances.contains(recordName))
                return 0;
            mdns_record_txt_t txt[32];
            size_t count = mdns_record_parse_txt(data, size, record_offset, record_length,
                                                 txt, sizeof(txt) / sizeof(txt[0]));
            MdnsBrowseInstance& inst = ctx->instances[recordName];
            for (size_t i = 0; i < count; ++i) {
                const QString key = QString::fromUtf8(txt[i].key.str,
                                                      static_cast<qsizetype>(txt[i].key.length)).toLower();
                const QString value = QString::fromUtf8(txt[i].value.str,
                                                        static_cast<qsizetype>(txt[i].value.length));
                if (!key.isEmpty())
                    inst.txt.insert(key, value);
            }
            break;
        }
        case MDNS_RECORDTYPE_A: {
            // Keep every A record seen — the SRV that needs it may arrive in a
            // later packet, so we cannot filter by "targets we know about" yet.
            struct sockaddr_in addr;
            mdns_record_parse_a(data, size, record_offset, record_length, &addr);
            ctx->addresses.insert(recordName,
                                  QHostAddress(ntohl(addr.sin_addr.s_addr)).toString());
            break;
        }
        default:
            break;
    }
    return 0;
}

// Open a query socket, and report which local port it actually landed on.
//
// THE PORT DECIDES WHO ANSWERS US. mjansson sets the QU (unicast-response) bit
// on every query unless getsockname() says the socket is on 5353
// (mdns.h:1085-1092). A query from an ephemeral port is a "legacy" query under
// RFC 6762 section 6.7, which a responder must answer by UNICAST — so the reply
// depends on the responder being able to address this host directly, and an
// openscale scale has been measured refusing to answer a peer it has no fresh
// path to for hours at a time. From 5353 the query is ordinary, the answer comes
// back multicast to the group, and nothing per-peer is involved. Preferring 5353
// is the openscale maintainers' own recommendation for working around it from the
// client side.
//
// It is preferred, not required, because the bind can legitimately fail — some
// platform or some other process may hold 5353 without SO_REUSEPORT — and a
// legacy query that sometimes works beats no socket at all.
//
// SO THIS PREFERS 5353 EVERYWHERE EXCEPT ANDROID, where it is measurably worse
// — see the switch below for that measurement, which is the important one.
//
// A separate defect was found while chasing this and is worth keeping here
// because it is real on its own: multicast reception on Android requires a held
// WifiManager.MulticastLock, and the app was not holding one. The lock belonged
// to ShotServer, whose `shotServer/enabled` setting defaults to FALSE, so on a
// default install no lock was ever taken while three comments in this tree
// described it as held "for the whole app lifetime".
//
// That fix stands, but note what it did NOT do: it was offered as the
// explanation for the older "5353 sees nothing on Android" measurement, and it
// is not. With the lock demonstrably held, 5353 on Android still receives
// records=0 for every host. Two independent problems, not one.
int openQuerySocket(int* boundPortOut)
{
    if (boundPortOut) *boundPortOut = 0;

    const MdnsResolver::QueryPort want = g_queryPort.load(std::memory_order_relaxed);

    // ONE decision, in one place, and readable by the test suite —
    // queryPortUsesMdnsPort() carries the platform branch and the reasoning.
    // Duplicating that branch here is exactly how it would drift from what is
    // asserted, and this is the invariant that shipped broken once already:
    // binding 5353 on Android loses every inbound packet to the system mDNS
    // daemon that already owns the port, so records=0 for EVERY host including
    // the MQTT broker's ".local" name.
    const bool try5353 = MdnsResolver::queryPortUsesMdnsPort();
    const bool allowFallback = want == MdnsResolver::QueryPort::Auto;

    int sock = -1;
    if (try5353) {
        struct sockaddr_in bindAddr;
        memset(&bindAddr, 0, sizeof(bindAddr));
        bindAddr.sin_family = AF_INET;
        bindAddr.sin_addr.s_addr = INADDR_ANY;
        bindAddr.sin_port = htons(MDNS_PORT);
        // SO_REUSEADDR and SO_REUSEPORT are set by the library itself
        // (mdns.h:405-408), so sharing 5353 with a system daemon is expected
        // rather than something to arrange here.
        sock = mdns_socket_open_ipv4(&bindAddr);
        if (sock < 0 && !allowFallback)
            return -1;
    }
    if (sock < 0) {
        struct sockaddr_in bindAddr;
        memset(&bindAddr, 0, sizeof(bindAddr));
        bindAddr.sin_family = AF_INET;
        bindAddr.sin_addr.s_addr = INADDR_ANY;
        sock = mdns_socket_open_ipv4(&bindAddr);
    }
    if (sock < 0)
        return -1;

    // Report the port the kernel gave us, not the one we asked for: those differ
    // whenever the 5353 bind failed, and that difference is the whole diagnostic.
    if (boundPortOut) {
        struct sockaddr_in local;
        socklen_t len = sizeof(local);
        if (getsockname(sock, reinterpret_cast<struct sockaddr*>(&local), &len) == 0)
            *boundPortOut = ntohs(local.sin_port);
    }
    return sock;
}

}  // namespace

namespace MdnsResolver {

QString resolveHostname(const QString& hostname, int timeoutMs,
                        ResolveStats* stats, const std::atomic<bool>* cancel)
{
    // Held for the whole lookup. Without it Android's Wi-Fi driver silently drops
    // every multicast frame addressed to the group, so a 5353 socket — whose
    // answers come back multicast — receives nothing at all while the sends and
    // the group join both report success. See MulticastLock.
    MulticastLock::Holder multicastLock;

    int boundPort = 0;
    int sock = openQuerySocket(&boundPort);
    if (sock < 0) {
        qWarning() << "[MdnsResolver] socket open FAILED for" << hostname
                   << "errno=" << errno;
        if (stats)
            stats->error = QString("mDNS socket open failed (errno %1)").arg(errno);
        return {};
    }
    if (stats) {
        stats->socketOpened = true;
        stats->boundPort = boundPort;
    }

    char buffer[2048];
    QByteArray hostBytes = hostname.toUtf8();

    MdnsResolveContext ctx;
    ctx.hostname = hostBytes;
    if (ctx.hostname.endsWith('.'))
        ctx.hostname.chop(1);
    ctx.verbose = false;  // flip to true to log every record seen (verbose probe diagnostics)

    // srcPort is not decoration: 5353 means an ordinary query answered by
    // multicast, anything else a legacy query the responder must unicast back.
    // Without it, "records=0" cannot be read.
    qDebug().noquote() << "[MdnsResolver] start host=" << hostname
                       << "timeout=" << timeoutMs << "ms sock=" << sock
                       << "srcPort=" << boundPort;

    int sendCount = 0;
    int sendOk = 0;  // successful sends — distinguishes "no responder" from "couldn't send"

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
        if (cancel && cancel->load(std::memory_order_relaxed))
            break;   // probe superseded or app quitting — release the pool thread
        if (deadline.elapsed() >= nextSendAt) {
            // Best-effort: a failed send is not fatal (transient ENOBUFS on a
            // congested interface) — keep polling and retry on the next tick.
            int sendRet = mdns_query_send(sock, MDNS_RECORDTYPE_A, hostBytes.constData(),
                                          static_cast<size_t>(hostBytes.size()),
                                          buffer, sizeof(buffer), 0);
            ++sendCount;
            if (sendRet >= 0) ++sendOk;
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
                       << "srcPort=" << boundPort
                       << "elapsed=" << deadline.elapsed() << "ms";

    // Distinguish a transport failure (every query send failed — e.g. no
    // multicast route / persistent ENOBUFS) from a silent responder, so triage
    // doesn't conflate "couldn't ask" with "asked but got no answer".
    if (sendCount > 0 && sendOk == 0) {
        qWarning().noquote() << "[MdnsResolver] all" << sendCount
                             << "query sends FAILED for" << hostname
                             << "— transport problem, not necessarily an absent responder";
        if (stats)
            stats->error = QString("all %1 mDNS query sends failed — transport problem, "
                                   "not necessarily an absent responder").arg(sendCount);
    }

    if (stats) {
        stats->queries = sendCount;
        stats->sendsOk = sendOk;
        stats->recordsSeen = ctx.recordsSeen;
        stats->aRecordsSeen = ctx.aRecordsSeen;
    }

    return ctx.resolvedIp;
}

QVector<ServiceInstance> browseServiceMjansson(const QString& serviceType, int timeoutMs,
                                              const std::function<void(const ServiceInstance&)>& onResolved,
                                              BrowseStats* stats,
                                              const std::atomic<bool>* cancel)
{
    if (stats) stats->backend = QStringLiteral("mjansson");
    // Same reason as resolveHostname() — a browse's answers are multicast when
    // the query goes out from 5353, and Android filters those without this.
    MulticastLock::Holder multicastLock;

    // Same socket policy as resolveHostname() — see openQuerySocket() for why the
    // source port decides whether the responder answers us at all.
    int boundPort = 0;
    int sock = openQuerySocket(&boundPort);
    if (sock < 0) {
        qWarning() << "[MdnsResolver] browse socket open FAILED for" << serviceType
                   << "errno=" << errno;
        if (stats) stats->error = QString("socket open failed (errno %1)").arg(errno);
        return {};
    }
    if (stats) stats->boundPort = boundPort;

    char buffer[2048];

    MdnsBrowseContext ctx;
    ctx.serviceType = serviceType.toUtf8().toLower();
    if (ctx.serviceType.endsWith('.'))
        ctx.serviceType.chop(1);

    qDebug().noquote() << "[MdnsResolver] browse start service=" << serviceType
                       << "timeout=" << timeoutMs << "ms sock=" << sock
                       << "srcPort=" << boundPort;

    // Retransmit for the same reason resolveHostname does: the scale's ESP32
    // shares one radio between BLE and WiFi and routinely misses a single
    // multicast query while BLE-connected.
    constexpr int kRetransmitMs = 750;

    QElapsedTimer deadline;
    deadline.start();
    qint64 nextSendAt = 0;
    int sendCount = 0;
    int sendOk = 0;

    while (deadline.elapsed() < timeoutMs) {
        if (cancel && cancel->load(std::memory_order_relaxed))
            break;   // scan ended or app quitting — do not hold the pool thread
        if (deadline.elapsed() >= nextSendAt) {
            // The PTR query is the browse. Responders bundle the matching SRV,
            // TXT and A records into the same packet as additional records, so
            // one exchange usually yields everything; the follow-up queries
            // below cover responders that don't.
            int sendRet = mdns_query_send(sock, MDNS_RECORDTYPE_PTR,
                                          ctx.serviceType.constData(),
                                          static_cast<size_t>(ctx.serviceType.size()),
                                          buffer, sizeof(buffer), 0);
            ++sendCount;
            if (sendRet >= 0) ++sendOk;

            // Chase anything still incomplete: an instance with no SRV yet, and
            // an SRV target with no address yet. Cheap (a couple of unicast-
            // requested queries) and it is what turns a partial answer into a
            // usable row rather than a dropped one.
            //
            // Count these like the PTR query rather than discarding the return.
            // If the chase sends fail systematically (congested interface,
            // persistent ENOBUFS) the instance ends up logged only as "dropped
            // unresolved (srv=no addr=no)" — identical to a responder that
            // never answered, when the truth is we never managed to ask.
            for (auto it = ctx.instances.cbegin(); it != ctx.instances.cend(); ++it) {
                int chaseRet = 0;
                if (it->port == 0 || it->target.isEmpty()) {
                    const QByteArray& n = it.key();
                    chaseRet = mdns_query_send(sock, MDNS_RECORDTYPE_SRV, n.constData(),
                                               static_cast<size_t>(n.size()),
                                               buffer, sizeof(buffer), 0);
                } else if (!ctx.addresses.contains(it->target)) {
                    const QByteArray& t = it->target;
                    chaseRet = mdns_query_send(sock, MDNS_RECORDTYPE_A, t.constData(),
                                               static_cast<size_t>(t.size()),
                                               buffer, sizeof(buffer), 0);
                } else {
                    continue;  // nothing to chase for this instance
                }
                ++sendCount;
                if (chaseRet >= 0) ++sendOk;
            }

            nextSendAt = deadline.elapsed() + kRetransmitMs;
        }

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
            // Anything else (EBADF, EINVAL, ENOMEM) aborts the browse. Without
            // recording it the summary is indistinguishable from an empty
            // network, which is the exact confusion BrowseStats exists to stop.
            qWarning() << "[MdnsResolver] browse select() failed errno=" << errno;
            if (stats && stats->error.isEmpty())
                stats->error = QString("select() failed (errno %1) — browse aborted early").arg(errno);
            break;
        }
        if (ret == 0) continue;

        mdns_query_recv(sock, buffer, sizeof(buffer), mdnsBrowseCallback, &ctx, 0);

        // Report anything that just became complete, instead of making the
        // caller wait out the full deadline for a scale that answered in 200 ms.
        if (onResolved) {
            for (auto it = ctx.instances.cbegin(); it != ctx.instances.cend(); ++it) {
                if (ctx.reported.contains(it.key()))
                    continue;
                if (!browseInstanceResolved(it->target, it->port,
                                            ctx.addresses.contains(it->target)))
                    continue;
                ctx.reported.insert(it.key());
                onResolved(makeInstance(it.key(), *it, ctx));
            }
        }
    }

    mdns_socket_close(sock);

    // Join: an instance is only a result once it has BOTH an SRV (target+port)
    // and an address for that target. Everything else is a ghost — a stale
    // registration whose PTR is still cached but whose SRV/A have expired. Half
    // the instances on the reference network were exactly this.
    QVector<ServiceInstance> results;
    int dropped = 0;
    for (auto it = ctx.instances.cbegin(); it != ctx.instances.cend(); ++it) {
        const QString instanceLabel = stripServiceSuffix(it.key(), ctx.serviceType);
        if (!browseInstanceResolved(it->target, it->port,
                                    ctx.addresses.contains(it->target))) {
            ++dropped;
            qDebug().noquote() << "[MdnsResolver] browse dropped unresolved instance="
                               << instanceLabel
                               << "(srv=" << (it->target.isEmpty() ? "no" : "yes")
                               << " addr=" << (ctx.addresses.contains(it->target) ? "yes" : "no") << ")";
            continue;
        }
        results.append(makeInstance(it.key(), *it, ctx));
    }

    qDebug().noquote() << "[MdnsResolver] browse done service=" << serviceType
                       << "resolved=" << results.size()
                       << "dropped=" << dropped
                       << "queries=" << sendCount
                       << "records=" << ctx.recordsSeen
                       << "srcPort=" << boundPort
                       << "elapsed=" << deadline.elapsed() << "ms";

    if (stats) {
        stats->instancesSeen = static_cast<int>(ctx.instances.size());
        stats->resolved = static_cast<int>(results.size());
        stats->dropped = dropped;
        stats->elapsedMs = deadline.elapsed();
        if (sendCount > 0 && sendOk == 0)
            stats->error = QStringLiteral("every multicast query send failed");
    }

    if (sendCount > 0 && sendOk == 0)
        qWarning().noquote() << "[MdnsResolver] all" << sendCount
                             << "browse query sends FAILED for" << serviceType
                             << "— transport problem, not necessarily an absent responder";

    return results;
}

}  // namespace MdnsResolver

#endif  // !Q_OS_IOS

#ifdef Q_OS_DARWIN

// Apple: browse through the system Bonjour API (mDNSResponder) rather than a
// raw multicast socket. This is not a style preference — a raw socket to
// 224.0.0.251 on iOS requires com.apple.developer.networking.multicast, an
// entitlement Apple grants only by per-app application. The daemon already
// holds the multicast privilege, so going through it needs no entitlement; the
// app just declares the service type in NSBonjourServices.

#include <QDeadlineTimer>
#include <QDebug>
#include <QElapsedTimer>
#include <QHash>
#include <QHostAddress>
#include <QPair>
#include <QSet>

#include <dns_sd.h>
#include <arpa/inet.h>
#include <sys/select.h>

#include <cerrno>

namespace {

// One in-flight browse. All the DNSServiceRefs share a single connection so
// there is exactly one fd to select on.
struct AppleBrowseContext {
    DNSServiceRef connection = nullptr;
    QVector<DNSServiceRef> children;   // browse + per-instance resolve/addr refs

    // Instance name -> partial data, mirroring the non-Apple path's join.
    struct Pending {
        QString instanceName;
        QString hostname;
        quint16 port = 0;
        QMap<QString, QString> txt;
        bool haveResolve = false;
        QString address;
    };
    QHash<QString, Pending> pending;
    QSet<QString> reported;
    // Per-callback contexts handed to the C API. They must outlive the browse
    // (the daemon calls back into them at will) and are freed only at teardown,
    // so they are owned here rather than by the callbacks. LeakSanitizer is
    // Linux-only and this file is Apple-only, so nothing would ever catch these
    // going astray — hence the explicit ownership.
    QVector<QPair<AppleBrowseContext*, QString>*> callbackContexts;
    QVector<MdnsResolver::ServiceInstance> results;
    std::function<void(const MdnsResolver::ServiceInstance&)> onResolved;
    int addsSeen = 0;
    int removesSeen = 0;
    bool permissionDenied = false;
};

// Turn a DNSServiceErrorType into something a user reading a shared log can act
// on. The permission case matters most and is easy to get wrong: denial arrives
// SYNCHRONOUSLY from DNSServiceBrowse() as kDNSServiceErr_NoAuth (-65555), not
// via the browse callback. Observed on macOS 2026-07-28 — the code originally
// only checked for it in the callback and so reported a bare error number for
// the one failure a user can actually fix.
QString describeBrowseError(DNSServiceErrorType err)
{
    switch (err) {
        case kDNSServiceErr_NoAuth:
        case kDNSServiceErr_PolicyDenied:
            // TWO causes, and they are easy to confuse. The obvious one is the
            // user denying Local Network access. The other is the app not
            // declaring this service type in NSBonjourServices — which cost a
            // debugging round on 2026-07-28, because a missing plist entry
            // produces the identical error and sends you to System Settings to
            // fix something that is not broken. Name both.
            return QString("browse refused (err %1) — either Local Network access is "
                           "denied for this app (System Settings > Privacy & Security > "
                           "Local Network), or the service type is not listed in "
                           "NSBonjourServices in the app's Info.plist")
                       .arg(err);
        case kDNSServiceErr_Firewall:
            return QString("blocked by firewall (err %1)").arg(err);
        default:
            return QString("DNSServiceBrowse failed (err %1)").arg(err);
    }
}

void appleEmitIfComplete(AppleBrowseContext* ctx, const QString& key)
{
    auto it = ctx->pending.find(key);
    if (it == ctx->pending.end())
        return;
    // Resolve-before-display, exactly as on the non-Apple path: an instance is
    // only real once it has BOTH the SRV data and an address. A browse routinely
    // lists stale registrations that never get this far.
    if (!it->haveResolve || it->address.isEmpty() || it->port == 0)
        return;
    if (ctx->reported.contains(key))
        return;
    ctx->reported.insert(key);

    MdnsResolver::ServiceInstance si;
    si.instanceName = it->instanceName;
    si.hostname = it->hostname;
    si.address = it->address;
    si.port = it->port;
    si.txt = it->txt;
    ctx->results.append(si);
    if (ctx->onResolved)
        ctx->onResolved(si);
}

void DNSSD_API appleAddrReply(DNSServiceRef, DNSServiceFlags,
                              uint32_t, DNSServiceErrorType errorCode,
                              const char* /*hostname*/, const struct sockaddr* address,
                              uint32_t, void* context)
{
    auto* pair = static_cast<QPair<AppleBrowseContext*, QString>*>(context);
    if (errorCode != kDNSServiceErr_NoError || !address)
        return;
    if (address->sa_family != AF_INET)
        return;  // IPv4 only, matching the rest of the WiFi-scale path

    char buf[INET_ADDRSTRLEN] = {0};
    const auto* in = reinterpret_cast<const struct sockaddr_in*>(address);
    if (!inet_ntop(AF_INET, &in->sin_addr, buf, sizeof(buf)))
        return;

    AppleBrowseContext* ctx = pair->first;
    auto it = ctx->pending.find(pair->second);
    if (it == ctx->pending.end())
        return;
    it->address = QString::fromLatin1(buf);
    appleEmitIfComplete(ctx, pair->second);
}

void DNSSD_API appleResolveReply(DNSServiceRef, DNSServiceFlags,
                                 uint32_t interfaceIndex, DNSServiceErrorType errorCode,
                                 const char* /*fullname*/, const char* hosttarget,
                                 uint16_t port, uint16_t txtLen,
                                 const unsigned char* txtRecord, void* context)
{
    auto* pair = static_cast<QPair<AppleBrowseContext*, QString>*>(context);
    if (errorCode != kDNSServiceErr_NoError)
        return;

    AppleBrowseContext* ctx = pair->first;
    auto it = ctx->pending.find(pair->second);
    if (it == ctx->pending.end())
        return;

    QString host = QString::fromUtf8(hosttarget);
    if (host.endsWith(QLatin1Char('.')))
        host.chop(1);
    it->hostname = host;
    it->port = ntohs(port);
    it->haveResolve = true;

    // TXT: every key optional, keys lowercased — a scale on fw 3.1.12 publishes
    // no "name" key at all, so absence must be normal rather than exceptional.
    const uint16_t count = TXTRecordGetCount(txtLen, txtRecord);
    for (uint16_t i = 0; i < count; ++i) {
        char key[256] = {0};
        uint8_t valueLen = 0;
        const void* valuePtr = nullptr;
        if (TXTRecordGetItemAtIndex(txtLen, txtRecord, i, sizeof(key), key,
                                    &valueLen, &valuePtr) != kDNSServiceErr_NoError)
            continue;
        const QString k = QString::fromUtf8(key).toLower();
        const QString v = valuePtr
            ? QString::fromUtf8(static_cast<const char*>(valuePtr), valueLen)
            : QString();
        if (!k.isEmpty())
            it->txt.insert(k, v);
    }

    // Chase the address for the SRV target.
    DNSServiceRef addrRef = ctx->connection;
    auto* addrCtx = new QPair<AppleBrowseContext*, QString>(ctx, pair->second);
    ctx->callbackContexts.append(addrCtx);  // freed at browse teardown
    if (DNSServiceGetAddrInfo(&addrRef, kDNSServiceFlagsShareConnection,
                              interfaceIndex, kDNSServiceProtocol_IPv4,
                              hosttarget, appleAddrReply, addrCtx)
            == kDNSServiceErr_NoError) {
        ctx->children.append(addrRef);
    }
}

void DNSSD_API appleBrowseReply(DNSServiceRef, DNSServiceFlags flags,
                                uint32_t interfaceIndex, DNSServiceErrorType errorCode,
                                const char* serviceName, const char* regtype,
                                const char* replyDomain, void* context)
{
    auto* ctx = static_cast<AppleBrowseContext*>(context);
    if (errorCode != kDNSServiceErr_NoError) {
        if (errorCode == kDNSServiceErr_PolicyDenied
                || errorCode == kDNSServiceErr_NoAuth) {
            // Local Network permission refused. Distinct from "nothing answered"
            // — the list looks identical either way, so only the log can tell
            // the user which happened.
            //
            // NOTE: in practice denial usually arrives synchronously from
            // DNSServiceBrowse() instead of here (see describeBrowseError), so
            // this path is a backstop, not the main detection.
            ctx->permissionDenied = true;
        }
        return;
    }

    const QString key = QString::fromUtf8(serviceName);

    if (!(flags & kDNSServiceFlagsAdd)) {
        // A withdrawal. Logged, never applied: within a scan cycle the list is
        // add-only, so a row the user has already seen is not retracted under
        // their finger. The next scan rebuilds from scratch.
        ctx->removesSeen++;
        qDebug().noquote() << "[MdnsResolver] browse withdrawal (not applied mid-scan) instance="
                           << key;
        return;
    }

    ctx->addsSeen++;
    if (!ctx->pending.contains(key)) {
        AppleBrowseContext::Pending p;
        p.instanceName = key;
        ctx->pending.insert(key, p);
    }

    DNSServiceRef resolveRef = ctx->connection;
    auto* resolveCtx = new QPair<AppleBrowseContext*, QString>(ctx, key);
    ctx->callbackContexts.append(resolveCtx);  // freed at browse teardown
    if (DNSServiceResolve(&resolveRef, kDNSServiceFlagsShareConnection,
                          interfaceIndex, serviceName, regtype, replyDomain,
                          appleResolveReply, resolveCtx) == kDNSServiceErr_NoError) {
        ctx->children.append(resolveRef);
    }
}

}  // namespace

namespace MdnsResolver {

QVector<ServiceInstance> browseServiceBonjour(const QString& serviceType, int timeoutMs,
                                              const std::function<void(const ServiceInstance&)>& onResolved,
                                              BrowseStats* stats,
                                              const std::atomic<bool>* cancel)
{
    QElapsedTimer elapsed;
    elapsed.start();
    if (stats) stats->backend = QStringLiteral("bonjour");
    // DNSServiceBrowse wants the regtype and domain separately, but our callers
    // pass one fully-qualified string ("_decentscale._tcp.local").
    QString regtype = serviceType;
    QString domain = QStringLiteral("local.");
    if (regtype.endsWith(QStringLiteral(".local")))
        regtype.chop(QStringLiteral(".local").size());
    else if (regtype.endsWith(QStringLiteral(".local.")))
        regtype.chop(QStringLiteral(".local.").size());

    AppleBrowseContext ctx;
    ctx.onResolved = onResolved;

    DNSServiceErrorType err = DNSServiceCreateConnection(&ctx.connection);
    if (err != kDNSServiceErr_NoError) {
        qWarning() << "[MdnsResolver] DNSServiceCreateConnection failed err=" << err;
        if (stats) stats->error = QString("DNSServiceCreateConnection failed (err %1)").arg(err);
        return {};
    }

    DNSServiceRef browseRef = ctx.connection;
    err = DNSServiceBrowse(&browseRef, kDNSServiceFlagsShareConnection,
                           kDNSServiceInterfaceIndexAny,
                           regtype.toUtf8().constData(), domain.toUtf8().constData(),
                           appleBrowseReply, &ctx);
    if (err != kDNSServiceErr_NoError) {
        qWarning() << "[MdnsResolver] DNSServiceBrowse failed err=" << err;
        if (stats) stats->error = describeBrowseError(err);
        DNSServiceRefDeallocate(ctx.connection);
        return {};
    }
    ctx.children.append(browseRef);

    qDebug().noquote() << "[MdnsResolver] browse start (Bonjour) service=" << serviceType
                       << "timeout=" << timeoutMs << "ms";

    // How often the loop below re-reads `cancel`. Unlike the mjansson path this
    // backend has nothing to retransmit — mDNSResponder owns the queries — so
    // the slice exists purely for cancellation latency.
    constexpr qint64 kCancelPollMs = 250;

    const int fd = DNSServiceRefSockFD(ctx.connection);
    if (fd < 0) {
        // Warn as well as record: every other failure path here does both, and
        // with a null `stats` this would otherwise produce an empty result set
        // with no diagnostic anywhere — the loop below simply never runs.
        qWarning() << "[MdnsResolver] DNSServiceRefSockFD returned no usable socket";
        if (stats)
            stats->error = QStringLiteral("DNSServiceRefSockFD returned no usable socket");
    }
    QDeadlineTimer deadline(timeoutMs);

    // Stay subscribed for the whole window rather than taking an early snapshot.
    // The daemon's first replies are its cache — stale instances included — and
    // its own pruning of those arrives seconds later.
    while (!deadline.hasExpired() && fd >= 0) {
        if (cancel && cancel->load(std::memory_order_relaxed))
            break;   // scan ended or app quitting — do not hold the pool thread
        if (ctx.permissionDenied)
            break;   // answer already known; waiting out the deadline only
                     // delays the user seeing the diagnostic
        const qint64 remaining = deadline.remainingTime();
        if (remaining <= 0) break;

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);
        // Wait in slices, not for the whole remaining deadline. `cancel` is only
        // read at the top of this loop, and on a quiet LAN nothing wakes
        // select() — so a full-deadline timeout meant a cancelled browse stayed
        // parked for up to 15 s, holding the QThreadPool thread that
        // ~QCoreApplication's unconditional waitForDone() then blocks on. That
        // is exactly the hang the cancel flag exists to prevent. The mjansson
        // loop already slices (kRetransmitMs) for the same reason.
        const qint64 slice = qMin<qint64>(remaining, kCancelPollMs);
        struct timeval tv;
        tv.tv_sec = static_cast<time_t>(slice / 1000);
        tv.tv_usec = static_cast<suseconds_t>((slice % 1000) * 1000);

        const int ret = select(fd + 1, &readfds, nullptr, nullptr, &tv);
        if (ret < 0) {
            if (errno == EINTR) continue;
            qWarning() << "[MdnsResolver] browse select() failed errno=" << errno;
            if (stats && stats->error.isEmpty())
                stats->error = QString("select() failed (errno %1) — browse aborted early").arg(errno);
            break;
        }
        if (ret == 0) continue;
        const DNSServiceErrorType procErr = DNSServiceProcessResult(ctx.connection);
        if (procErr != kDNSServiceErr_NoError) {
            // The mDNSResponder connection died mid-browse (daemon restart,
            // kDNSServiceErr_ServiceNotRunning). Anything collected so far is
            // still returned, but the caller must know the browse was cut short
            // rather than simply finding nothing more.
            qWarning() << "[MdnsResolver] DNSServiceProcessResult failed err=" << procErr;
            if (stats && stats->error.isEmpty())
                stats->error = QString("mDNSResponder connection failed mid-browse (err %1)").arg(procErr);
            break;
        }
    }

    // Deallocating the shared connection tears down every child ref with it.
    // Only after that is it safe to free the callback contexts — the daemon can
    // still call back until the connection is gone.
    DNSServiceRefDeallocate(ctx.connection);
    qDeleteAll(ctx.callbackContexts);
    ctx.callbackContexts.clear();

    const qsizetype dropped = ctx.pending.size() - ctx.results.size();
    qDebug().noquote() << "[MdnsResolver] browse done (Bonjour) service=" << serviceType
                       << "resolved=" << ctx.results.size()
                       << "dropped=" << dropped
                       << "adds=" << ctx.addsSeen
                       << "removes=" << ctx.removesSeen;

    if (stats) {
        stats->instancesSeen = static_cast<int>(ctx.pending.size());
        stats->resolved = static_cast<int>(ctx.results.size());
        stats->dropped = static_cast<int>(dropped);
        stats->withdrawals = ctx.removesSeen;
        stats->elapsedMs = elapsed.elapsed();
        if (ctx.permissionDenied && stats->error.isEmpty()) {
            // Same two-cause message as the synchronous path — a missing
            // NSBonjourServices entry produces this too, and sending the user to
            // System Settings for that would be a wild goose chase.
            stats->error = describeBrowseError(kDNSServiceErr_NoAuth);
        }
    }

    if (ctx.permissionDenied) {
        qWarning().noquote()
            << "[MdnsResolver] Local Network permission DENIED — the browse could not run."
            << "This is not an empty network; the user must grant Local Network access.";
    }

    return ctx.results;
}

}  // namespace MdnsResolver

#endif  // Q_OS_DARWIN

// ---------------------------------------------------------------------------
// Backend selection
// ---------------------------------------------------------------------------

#include <QDebug>

namespace MdnsResolver {

// Defined here, outside every platform guard, so the rule is one function on
// every platform rather than one per backend. See the header for why a ghost
// instance must not become a row.
bool browseInstanceResolved(const QByteArray& srvTarget, quint16 port, bool haveAddress)
{
    return !srvTarget.isEmpty() && port != 0 && haveAddress;
}

#ifdef Q_OS_IOS
// iOS never builds the mjansson path (it would need the multicast entitlement),
// and resolves ".local" through QHostInfo/Bonjour, so this is unreachable —
// present only so the TU links.
QString resolveHostname(const QString& /*hostname*/, int /*timeoutMs*/,
                        ResolveStats* stats, const std::atomic<bool>* /*cancel*/)
{
    if (stats)
        stats->error = QStringLiteral("mDNS A-record path is not compiled on iOS");
    return {};
}
#endif

namespace {
// Atomic because it is written from the main thread (the MCP diagnostic tool)
// and read from the browse worker. A torn enum read is unlikely in practice, but
// this is a genuine data race and the nightly sanitizer build compiles it.
std::atomic<BrowseBackend> g_backend{BrowseBackend::Auto};

// Resolves to a backend that ACTUALLY EXISTS on this platform. Requesting one
// that isn't compiled in must not be reported back as if it ran — the whole
// point of the switch is comparing two backends, and silently substituting would
// make a comparison of one backend against itself look like a match.
BrowseBackend resolveBackend(BrowseBackend requested)
{
#ifdef Q_OS_DARWIN
  #ifdef Q_OS_IOS
    Q_UNUSED(requested);
    return BrowseBackend::Bonjour;       // mjansson is not compiled on iOS
  #else
    // macOS defaults to MJANSSON, not Bonjour, and that is a deliberate
    // inversion of "ship what the platform prefers".
    //
    // macOS is not a production platform here — it is the development one. The
    // shipped populations are Android (hundreds of users) and iOS; the macOS
    // installs number about two, both of them developers. Bonjour is measurably
    // the better citizen on macOS (mDNSResponder's cache is always warm, so it
    // reaches a first row in 66-113 ms against mjansson's 160-270 ms on the
    // reference LAN) — but that ~100 ms is paid by two people, and what it buys
    // is that the browse path THREE platforms ship goes unexercised in daily use
    // by the only machine anyone develops on. That asymmetry is what let a
    // multi-hour Android discovery outage survive review, and BrowseBackend was
    // added to work around it by hand.
    //
    // Bonjour is NOT thereby untested: iOS ships it, and iOS is production. What
    // this does cost is early warning — an iOS release build is only compiled by
    // CI, so the Mac was the one place a Bonjour regression would be noticed
    // before users saw it. Run a browse with backend=bonjour before an iOS
    // release; that is the check this default gives up.
    //
    // Only the BROWSE moves. The hostname resolver stays on QHostInfo here
    // (see resolveHostnameResolver) precisely so the other iOS path keeps its
    // dev coverage — its mjansson variant is Android-only, and pointing the Mac
    // at it on demand is enough.
    if (requested == BrowseBackend::Auto)
        return BrowseBackend::Mjansson;
    return requested;                    // macOS has both
  #endif
#else
    // Bonjour does not exist off Apple, whatever was asked for.
    Q_UNUSED(requested);
    return BrowseBackend::Mjansson;
#endif
}
}  // namespace

void setBrowseBackend(BrowseBackend backend)
{
    g_backend.store(backend, std::memory_order_relaxed);
}

BrowseBackend browseBackend() { return g_backend.load(std::memory_order_relaxed); }

QString activeBrowseBackendName()
{
    return resolveBackend(g_backend.load(std::memory_order_relaxed)) == BrowseBackend::Bonjour
        ? QStringLiteral("bonjour") : QStringLiteral("mjansson");
}

namespace {
// Same race as g_backend: written by the MCP tool on the main thread, read by
// the lookup that WifiScaleDiscovery starts.
std::atomic<HostnameResolver> g_hostnameResolver{HostnameResolver::Auto};

// Same rule as resolveBackend(): report what CAN run here, never what was asked
// for. On iOS mjansson is not compiled at all, so a request for it is the system
// resolver — and saying otherwise would attribute a Bonjour result to a backend
// that does not exist in the binary.
HostnameResolver resolveHostnameResolver(HostnameResolver requested)
{
#ifdef Q_OS_IOS
    Q_UNUSED(requested);
    return HostnameResolver::System;
#else
    if (requested != HostnameResolver::Auto)
        return requested;
  #ifdef Q_OS_ANDROID
    // Android's getaddrinfo returns NXDOMAIN for ".local", so the direct query
    // is not a diagnostic option there — it is the only thing that works.
    return HostnameResolver::Mjansson;
  #else
    return HostnameResolver::System;
  #endif
#endif
}
}  // namespace

void setQueryPort(QueryPort port)
{
    g_queryPort.store(port, std::memory_order_relaxed);
}

QueryPort queryPort() { return g_queryPort.load(std::memory_order_relaxed); }

bool queryPortUsesMdnsPort()
{
    switch (g_queryPort.load(std::memory_order_relaxed)) {
        case QueryPort::Mdns:      return true;
        case QueryPort::Ephemeral: return false;
        case QueryPort::Auto:      break;
    }
    // See openQuerySocket() for the measurement behind the Android exclusion.
#ifdef Q_OS_ANDROID
    return false;
#else
    return true;
#endif
}

QString queryPortName()
{
    // The REQUEST, unlike the two backend selectors, which report what ran. Here
    // "what ran" is a number the socket knows and this function does not — every
    // browse and lookup reports its own ResolveStats/BrowseStats::boundPort,
    // which is the honest answer and can differ between two calls under one
    // policy if a bind fails once.
    switch (g_queryPort.load(std::memory_order_relaxed)) {
        case QueryPort::Mdns:      return QStringLiteral("mdns");
        case QueryPort::Ephemeral: return QStringLiteral("ephemeral");
        case QueryPort::Auto:      break;
    }
    return QStringLiteral("auto");
}

void setHostnameResolver(HostnameResolver resolver)
{
    g_hostnameResolver.store(resolver, std::memory_order_relaxed);
}

HostnameResolver hostnameResolver()
{
    return g_hostnameResolver.load(std::memory_order_relaxed);
}

QString activeHostnameResolverName()
{
    return useDirectHostnameResolver() ? QStringLiteral("mjansson")
                                       : QStringLiteral("system");
}

bool useDirectHostnameResolver()
{
    return resolveHostnameResolver(g_hostnameResolver.load(std::memory_order_relaxed))
        == HostnameResolver::Mjansson;
}

QVector<ServiceInstance> browseService(const QString& serviceType, int timeoutMs,
                                       const std::function<void(const ServiceInstance&)>& onResolved,
                                       BrowseStats* stats,
                                       const std::atomic<bool>* cancel)
{
    // Fresh stats per call: callers may reuse one object, and silently merging
    // two browses' counters would be worse than not reporting them at all.
    if (stats) *stats = BrowseStats{};

    const BrowseBackend backend = resolveBackend(g_backend.load(std::memory_order_relaxed));

    if (backend == BrowseBackend::Bonjour) {
#ifdef Q_OS_DARWIN
        return browseServiceBonjour(serviceType, timeoutMs, onResolved, stats, cancel);
#endif
    }

#ifndef Q_OS_IOS
    return browseServiceMjansson(serviceType, timeoutMs, onResolved, stats, cancel);
#else
    // Unreachable: iOS always resolves to Bonjour above.
    qWarning() << "[MdnsResolver] no browse backend available";
    return {};
#endif
}

}  // namespace MdnsResolver
