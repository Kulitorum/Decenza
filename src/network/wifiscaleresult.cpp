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

    // Port from SRV, but let an explicit TXT port override it if present and
    // sane — the firmware advertises 80 in both places today.
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

QVector<WifiScaleResult> mergeAndDedupe(const QVector<WifiScaleResult>& browse,
                                        const QVector<WifiScaleResult>& fallback)
{
    QVector<WifiScaleResult> out;
    QSet<QString> seenAddresses;

    // Browse first: it wins every collision, because it carries metadata the
    // fallback path structurally cannot produce.
    for (const WifiScaleResult& r : browse) {
        if (r.address.isEmpty() || seenAddresses.contains(r.address))
            continue;
        seenAddresses.insert(r.address);
        out.append(r);
    }

    for (const WifiScaleResult& r : fallback) {
        // An address-less fallback hit is not usable and cannot be deduped.
        if (r.address.isEmpty() || seenAddresses.contains(r.address))
            continue;
        seenAddresses.insert(r.address);
        out.append(r);
    }

    return out;
}

namespace {

// Strip a DNS-SD collision suffix: "Half Decent Scale-2" -> "Half Decent Scale".
// Only a trailing "-<digits>" is removed, so a scale legitimately named
// "kitchen-2" by its owner keeps its name (that arrives via TXT `name`, and the
// instance label would then read "Half Decent Scale (kitchen-2)").
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
    // Fallback-only hit: all we have is the hostname. "hds.local" -> "hds".
    QString host = result.hostname;
    const qsizetype dot = host.indexOf(QLatin1Char('.'));
    if (dot > 0)
        host = host.left(dot);
    return host;
}

}  // namespace

QString displayName(const WifiScaleResult& result, bool ambiguous)
{
    QString label = rawLabel(result);
    if (label.isEmpty())
        label = result.address;
    if (ambiguous && !result.address.isEmpty())
        label += QStringLiteral(" (%1)").arg(result.address);
    return label;
}

bool labelsCollide(const WifiScaleResult& a, const WifiScaleResult& b)
{
    return baseLabel(rawLabel(a)).compare(baseLabel(rawLabel(b)), Qt::CaseInsensitive) == 0;
}

}  // namespace WifiScaleResultUtil
