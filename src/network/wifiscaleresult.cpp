#include "wifiscaleresult.h"

#include <QRegularExpression>
#include <QSet>

namespace WifiScaleResultUtil {

QString normalizeFirmwareVersion(const QString& raw)
{
    QString value = raw.trimmed();
    if (value.isEmpty())
        return {};

    // "FW: 3.1.12" -> "3.1.12". Observed on shipping firmware; the enum-looking
    // bare version this code originally assumed does not exist on the wire.
    static const QRegularExpression prefix(QStringLiteral("^fw\\s*:\\s*"),
                                           QRegularExpression::CaseInsensitiveOption);
    value.remove(prefix);
    return value.trimmed();
}

WifiScaleResult fromBrowseTxt(const QString& instanceName,
                              const QString& hostname,
                              const QString& address,
                              quint16 port,
                              const QMap<QString, QString>& txt)
{
    WifiScaleResult r;
    r.foundBy = WifiScaleResult::Source::Browse;
    r.instanceName = instanceName;
    r.hostname = hostname;
    r.address = address;

    // Port comes from SRV only; the firmware publishes no TXT port key. 80 is
    // the firmware default and the fallback when SRV gave us nothing.
    r.port = port != 0 ? port : quint16(80);

    // Each of these is optional. Absent means "use the default", never "reject".
    if (txt.contains(QStringLiteral("name")))
        r.mdnsName = txt.value(QStringLiteral("name"));
    if (txt.contains(QStringLiteral("path"))) {
        const QString p = txt.value(QStringLiteral("path")).trimmed();
        if (!p.isEmpty())
            r.path = p.startsWith(QLatin1Char('/')) ? p : QLatin1Char('/') + p;
    }
    if (txt.contains(QStringLiteral("fw")))
        r.firmwareVersion = normalizeFirmwareVersion(txt.value(QStringLiteral("fw")));

    return r;
}

QString normalizeHostname(const QString& hostname)
{
    QString h = hostname.trimmed().toLower();
    while (h.endsWith(QLatin1Char('.')))
        h.chop(1);
    return h;
}

QString shortHostname(const QString& hostname)
{
    // Strip the ".local" suffix: the user named the scale "hdstest", and the
    // domain part is the same for every scale so it carries no information.
    QString host = normalizeHostname(hostname);
    const qsizetype dot = host.indexOf(QLatin1Char('.'));
    if (dot > 0)
        host = host.left(dot);
    return host;
}

QString wifiScaleLabel(const QString& hostname)
{
    const QString host = shortHostname(hostname);
    if (host.isEmpty())
        return QStringLiteral("Half Decent Scale");
    return QStringLiteral("Half Decent Scale (%1)").arg(host);
}

QString wifiScaleDisplayName(const QString& hostname)
{
    return QStringLiteral("%1 (WiFi)").arg(wifiScaleLabel(hostname));
}

namespace {
// Field-by-field equality. Written out rather than a defaulted operator==
// because that is C++20 and this project builds as C++17.
bool sameResult(const WifiScaleResult& a, const WifiScaleResult& b)
{
    return a.instanceName == b.instanceName
        && a.mdnsName == b.mdnsName
        && a.hostname == b.hostname
        && a.address == b.address
        && a.port == b.port
        && a.path == b.path
        && a.firmwareVersion == b.firmwareVersion
        && a.foundBy == b.foundBy;
}
}  // namespace

bool upsertByHostname(QVector<WifiScaleResult>& set, const WifiScaleResult& incoming)
{
    // Identity is the HOSTNAME, not the resolved address.
    //
    // The address is the least stable thing we have: DHCP moves it, and keying
    // on it would make the same scale appear as a new entry after a lease
    // change while its old entry lingered. The hostname survives that, it is
    // what the scale answers to, and it is already what the saved-address
    // scheme ("wifi:<hostname>") persists.
    //
    // A hardware address would be better still, but mDNS does not carry one —
    // we get hostname, address, instance name and TXT, and nothing else.
    //
    // The two discovery paths agree on this key: the browse reports the SRV
    // target ("hds.local") and the fallback reports the name it queried
    // ("hds.local"). Only case and a trailing dot can differ, hence the
    // normalization.
    const QString key = normalizeHostname(incoming.hostname);
    if (key.isEmpty())
        return false;
    // Still require an address — without one the entry cannot be connected to,
    // which means the instance never resolved.
    if (incoming.address.isEmpty())
        return false;

    for (WifiScaleResult& seen : set) {
        if (normalizeHostname(seen.hostname) != key)
            continue;

        // A browse hit supersedes anything: it carries instance name, port,
        // path and firmware the A-record path structurally cannot produce.
        if (incoming.foundBy == WifiScaleResult::Source::Browse) {
            // "Changed" has to mean changed. mDNS re-announces the same record
            // periodically, so a long browse re-delivers an identical result
            // several times; returning true for those would report a change on
            // every re-announce to any caller that trusts this rather than
            // re-deriving the difference itself.
            if (sameResult(seen, incoming))
                return false;
            seen = incoming;
            return true;
        }
        // A fallback hit for a scale we already know contributes only a fresh
        // address — which is exactly the DHCP case worth keeping current.
        if (seen.address != incoming.address) {
            seen.address = incoming.address;
            return true;
        }
        return false;
    }

    set.append(incoming);
    return true;
}

namespace {

// Strip a DNS-SD collision suffix: "Half Decent Scale-2" -> "Half Decent Scale".
//
// ONLY valid for a browse label. DNS-SD generates that suffix; a fallback hit's
// label is a bare hostname, where a trailing "-2" is something the USER typed —
// and this project's own fallback list probes hds/hds-2/hds-3, so stripping
// there would collapse two genuinely different scales onto one label and mark
// every multi-scale fallback discovery ambiguous.
QString baseLabel(const QString& instanceName)
{
    static const QRegularExpression suffix(QStringLiteral("-\\d+$"));
    QString s = instanceName;
    s.remove(suffix);
    return s.trimmed();
}

// What to call this scale before any disambiguation.
QString rawLabel(const WifiScaleResult& result)
{
    if (!result.instanceName.isEmpty())
        return result.instanceName;
    if (!result.mdnsName.isEmpty())
        return result.mdnsName;
    // Fallback-only hit: all we have is the hostname. Composed through the same
    // helper the CONNECTED scale's name uses, so one scale does not read
    // "hds" while discovered and "Half Decent Scale (hds)" once connected.
    return wifiScaleLabel(result.hostname);
}

}  // namespace

QString displayName(const WifiScaleResult& result, bool ambiguous)
{
    QString label = rawLabel(result);
    if (label.isEmpty())
        label = result.address;

    // The hostname goes in unconditionally, whatever the firmware chose to
    // publish. openscale only embeds the scale's name in its DNS-SD instance
    // label when the user has RENAMED it: a default scale advertises the bare
    // "Half Decent Scale", so its row read "Half Decent Scale (WiFi)" with no
    // hostname anywhere, while a renamed one beside it read "Half Decent Scale
    // (hdstest) (WiFi)". Two scales, one of them unidentifiable, and the
    // difference was a firmware detail rather than anything about the device.
    //
    // It was also inconsistent with itself: the same scale, once CONNECTED,
    // is named by wifiScaleDisplayName(), which has always put the hostname in.
    // The hostname is the identity everything else keys on — it is what
    // "wifi:<hostname>" persists and what upsertByHostname dedupes by — so it
    // is the one part of the label that has to be there.
    //
    // Skipped when the label already carries it, so a renamed scale does not
    // become "Half Decent Scale (hdstest) (hdstest)".
    const QString host = shortHostname(result.hostname);
    if (!host.isEmpty() && !label.contains(host, Qt::CaseInsensitive))
        label += QStringLiteral(" (%1)").arg(host);

    if (ambiguous && !result.address.isEmpty())
        label += QStringLiteral(" (%1)").arg(result.address);
    return label;
}

bool labelsCollide(const WifiScaleResult& a, const WifiScaleResult& b)
{
    // Suffix-strip only what DNS-SD could have suffixed (see baseLabel).
    const auto norm = [](const WifiScaleResult& r) {
        const QString raw = rawLabel(r);
        return r.foundBy == WifiScaleResult::Source::Browse ? baseLabel(raw) : raw;
    };
    return norm(a).compare(norm(b), Qt::CaseInsensitive) == 0;
}

QHash<QString, QString> labelsByHostname(const QVector<WifiScaleResult>& set)
{
    QHash<QString, QString> out;
    for (const WifiScaleResult& r : set) {
        const QString key = normalizeHostname(r.hostname);
        if (key.isEmpty())
            continue;
        bool ambiguous = false;
        for (const WifiScaleResult& other : set) {
            if (normalizeHostname(other.hostname) == key)
                continue;
            if (labelsCollide(r, other)) { ambiguous = true; break; }
        }
        out.insert(key, displayName(r, ambiguous));
    }
    return out;
}

}  // namespace WifiScaleResultUtil
