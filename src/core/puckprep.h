#pragma once

#include <QString>
#include <QStringList>
#include <QVariantMap>

// Puck-prep technique flags for an equipment package (add-puckprep-equipment).
//
// Unlike grinder/basket, puck prep has no vendor → model identity — it is the set
// of distribution/prep techniques the user ticks (WDT, shaker, …). The package's
// puck-prep IDENTITY is therefore the SET of enabled flags, stored as a canonical
// (sorted, comma-joined) string in the puckprep item's `model` column — which lets
// it reuse the exact brand/model identity machinery (dedup correlated-subquery
// match, optional upsert/delete) that grinder and basket use. The individual flags
// and the derived `distribution` rollup are reconstructed from that string at read
// time; nothing puck-prep-specific is stored in `attrs`.
namespace PuckPrep {

// The recorded flags, in DISPLAY order (the form renders them in this order). The
// stored canonical identity is these keys SORTED, so it is order-independent.
inline const QStringList& flagKeys()
{
    static const QStringList keys = {
        QStringLiteral("wdt"),
        QStringLiteral("shaker"),
        QStringLiteral("puckScreen"),
        QStringLiteral("paperFilter"),
        QStringLiteral("rdt"),
    };
    return keys;
}

// Canonical identity string for a flag map: the SET flag keys, sorted and
// comma-joined. "" = no puck prep. Order-independent by construction, so two maps
// that enable the same flags produce the same string (the dedup/fork key).
inline QString canonical(const QVariantMap& flags)
{
    QStringList on;
    for (const QString& k : flagKeys())
        if (flags.value(k).toBool())
            on << k;
    on.sort();
    return on.join(QLatin1Char(','));
}

// The flag keys enabled in a canonical string.
inline QStringList setFlags(const QString& canon)
{
    const QString c = canon.trimmed();
    if (c.isEmpty())
        return {};
    return c.split(QLatin1Char(','), Qt::SkipEmptyParts);
}

inline bool has(const QString& canon, const QString& key)
{
    return setFlags(canon).contains(key);
}

// True when a field map carries any puck-prep flag (namespaced "puckPrep_<key>",
// the form the QML dialog / MCP send and EquipmentPackageView::toVariantMap emits).
inline bool mapTouches(const QVariantMap& map)
{
    for (const QString& k : flagKeys())
        if (map.contains(QStringLiteral("puckPrep_") + k))
            return true;
    return false;
}

// Canonical string for the result of applying a field map's "puckPrep_<key>"
// overrides on top of a current canonical string. Flags absent from the map keep
// their current value, so a PARTIAL update (e.g. an MCP call setting only `wdt`)
// preserves the rest; the full-form case (all flags present) sets the exact state.
inline QString canonicalMerged(const QString& currentCanon, const QVariantMap& map)
{
    QVariantMap flags;
    for (const QString& k : flagKeys()) {
        bool v = has(currentCanon, k);
        const QString pk = QStringLiteral("puckPrep_") + k;
        if (map.contains(pk))
            v = map.value(pk).toBool();
        flags.insert(k, v);
    }
    return canonical(flags);
}

// Derived distribution rollup (the signal the AI advisor reads to branch its
// channeling guidance). WDT and shaker are BOTH deliberate distribution techniques
// and are weighted EQUALLY — which is "better" is genuinely contested (a good
// shaker / needle distributor matches or beats mediocre WDT, and WDT quality is
// highly technique-dependent), so the rollup does not rank them; it answers "did
// the user actively distribute, or is this dump-and-tamp?". RDT alone is
// anti-static declumping, not active distribution, so it counts as light. The
// other flags (puckScreen/paperFilter) are read individually and do not move the
// rollup. Pure function of the canonical string — never stored.
inline QString distribution(const QString& canon)
{
    if (has(canon, QStringLiteral("wdt")) || has(canon, QStringLiteral("shaker")))
        return QStringLiteral("thorough");
    if (has(canon, QStringLiteral("rdt")))
        return QStringLiteral("light");
    return QStringLiteral("none");
}

} // namespace PuckPrep
