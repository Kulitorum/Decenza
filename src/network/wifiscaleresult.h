#pragma once

#include <QHash>
#include <QMap>
#include <QMetaType>
#include <QString>
#include <QVector>

/**
 * One discovered WiFi scale, from either discovery path.
 *
 * Kept in its own header (rather than inside WifiScaleDiscovery) so the pure
 * helpers below can be unit-tested without a QObject, an event loop, or a
 * network — which matters because the mDNS transport itself is only compiled on
 * non-Apple platforms, so a macOS test run can never exercise it. Everything
 * that can be decided from data rather than from packets lives here and IS
 * covered on every platform.
 */
struct WifiScaleResult {
    // How this result was found. Drives dedupe precedence: a browse hit carries
    // instance name, port, path and firmware that the A-record path cannot.
    enum class Source { Browse, Fallback };

    QString instanceName;      // DNS-SD instance label; empty for fallback hits
    QString mdnsName;          // TXT "name"; OFTEN ABSENT even on a browse hit
    QString hostname;          // e.g. "hdstest.local"
    QString address;           // resolved IPv4 dotted quad
    quint16 port = 80;         // SRV port; 80 is the firmware default
    QString path = QStringLiteral("/snapshot");  // TXT "path"
    QString firmwareVersion;   // TXT "fw", prefix stripped; may be empty
    Source foundBy = Source::Fallback;
};

// Registered for QSignalSpy, which needs a metatype to capture signal arguments.
// NOT for cross-thread signal delivery: the worker→main hop is a lambda capture
// inside QMetaObject::invokeMethod, and resultFound() is then emitted on the
// main thread to a main-thread receiver, i.e. a direct connection.
Q_DECLARE_METATYPE(WifiScaleResult)

namespace WifiScaleResultUtil {

/**
 * Normalize the TXT "fw" value.
 *
 * The wire format is NOT a bare version: firmware publishes "FW: 3.1.12" —
 * a prefix plus a literal space. Strips a leading "FW:" (any case) and
 * surrounding whitespace. Anything that does not look like a version is
 * returned as-is rather than discarded, so an unexpected format still shows the
 * user something true instead of nothing.
 */
QString normalizeFirmwareVersion(const QString& raw);

/**
 * Build a result from a browse hit's TXT map. Every key is optional — a scale
 * on firmware 3.1.12 publishes no "name" key at all — so missing keys fall back
 * to defaults rather than failing the record.
 */
WifiScaleResult fromBrowseTxt(const QString& instanceName,
                              const QString& hostname,
                              const QString& address,
                              quint16 port,
                              const QMap<QString, QString>& txt);

/**
 * Insert `incoming` into `set`, or merge it into the existing entry for the same
 * scale. THIS is how results actually arrive — one at a time, from two
 * concurrent discovery paths — so this is the operation worth testing.
 *
 * Identity is the normalized HOSTNAME, not the resolved address — DHCP moves
 * addresses, and keying on one would strand the old entry and create a new row
 * for the same scale after a lease change. mDNS carries no hardware address, so
 * the hostname is the most stable identifier available.
 *
 * A browse hit supersedes a fallback entry for the same hostname, because it
 * carries instance name, port, path and firmware the A-record path cannot
 * produce. A fallback hit never overwrites a browse entry; it only refreshes the
 * address, which DHCP can move between scans.
 *
 * Results with no hostname or no address are rejected: an entry without an
 * address never resolved and cannot be connected to. Returns true if `set`
 * changed.
 */
bool upsertByHostname(QVector<WifiScaleResult>& set, const WifiScaleResult& incoming);

/** Lowercased, trailing dot removed — the canonical key for a scale. */
QString normalizeHostname(const QString& hostname);

/**
 * The display label for every result in `set`, keyed by normalized hostname.
 *
 * Derived across the whole set rather than per result, because ambiguity is a
 * property of the set: DNS-SD suffixes colliding instance names, so two
 * unrenamed scales arrive as "Half Decent Scale" and "Half Decent Scale-2" and
 * neither label identifies a physical scale. Those get their address appended.
 *
 * Callers should REBUILD their rows from this rather than patching labels in
 * place — a label that was correct when its row was created can be made
 * ambiguous by a later arrival.
 */
QHash<QString, QString> labelsByHostname(const QVector<WifiScaleResult>& set);

/**
 * The label for a scale's row in the discovered-devices list.
 *
 * `ambiguous` should be true when another row in the same list would otherwise
 * carry an indistinguishable label — DNS-SD suffixes colliding instance names
 * ("Half Decent Scale" / "Half Decent Scale-2"), which tells the user nothing
 * about which scale is which. When set, the address is appended.
 */
QString displayName(const WifiScaleResult& result, bool ambiguous);

/**
 * True when two results would render the same base label, so the caller can ask
 * displayName() to disambiguate. Compares the base label only, ignoring any
 * DNS-SD "-2"/"-3" collision suffix, since that suffix is an artifact of the
 * protocol rather than something the user set or recognizes.
 */
bool labelsCollide(const WifiScaleResult& a, const WifiScaleResult& b);

}  // namespace WifiScaleResultUtil
