#pragma once

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
    quint16 port = 80;         // TXT/SRV port; 80 is the firmware default
    QString path = QStringLiteral("/snapshot");  // TXT "path"
    QString firmwareVersion;   // TXT "fw", prefix stripped; may be empty
    Source foundBy = Source::Fallback;
};

// Carried across threads by queued connections (the mDNS worker posts results
// back to the object's thread) and inspected by QSignalSpy in tests, both of
// which need the type registered.
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
 * Merge browse and fallback results into the list the user sees.
 *
 * Deduped on resolved address, because that is the only identity both paths
 * establish — hostnames differ in form between them, and TXT data is absent
 * from the fallback path entirely. On a collision the browse result wins.
 *
 * Order is stable: browse results first in their original order, then any
 * fallback results that were not already covered.
 */
QVector<WifiScaleResult> mergeAndDedupe(const QVector<WifiScaleResult>& browse,
                                        const QVector<WifiScaleResult>& fallback);

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
