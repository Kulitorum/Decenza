// Profile-knowledge resolution cluster (change: restructure-kb-as-validated-json).
//
// WHY A SEPARATE TU: these `ShotSummarizer::` statics (loadProfileKnowledge,
// matchProfileKey, computeProfileKbId, getAnalysisFlags, ugs*, canonical
// NameForKbId, expertBandForKbId, allKbUgsEntries, buildProfileCatalog,
// crossProfileReferenceContent, …) form a cohesive KB-data layer that
// depends only on the `:/ai/*` resources + QString — NOT on the
// prompt/summary/dialing machinery the rest of shotsummarizer.cpp pulls.
// Keeping them separate lets offline consumers (the `shot_eval` tool,
// tst_shotrecord_cache) link a lean closure ({this TU} + ai.qrc).
//
// DATA SOURCE: resources/ai/profile_knowledge.json — a validated structured
// document (schema: resources/ai/profile_knowledge.schema.json; build-time
// gate: tools/validate_kb.py). Replaces the former line.startsWith() markdown
// scraper + the hardcoded C++ kBands table. Identity is a stable kebab `id`;
// resolution is exact-match-or-explicitly-unresolved over an alias→id map
// (the order-dependent greedy startsWith/contains fallback is DELETED).
#include "shotsummarizer.h"
#include "shotanalysis.h"  // ShotAnalysis::ExpertBand (expertBandForKbId return)

#include <cmath>
#include <limits>
#include <optional>
#include <QFile>
#include <QTextStream>
#include <QStringList>
#include <QSet>
#include <QMap>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QMutex>
#include <QMutexLocker>
#include <QDebug>

// Static members for profile knowledge cache
QMap<QString, ShotSummarizer::ProfileKnowledge> ShotSummarizer::s_profileKnowledge;
QMap<QString, QString> ShotSummarizer::s_aliasToId;
bool ShotSummarizer::s_knowledgeLoaded = false;

// Static cache for profile catalog (compact one-liner per KB profile)
QString ShotSummarizer::s_profileCatalog;

// Static cache for dial-in reference tables
QString ShotSummarizer::s_dialInReference;
bool ShotSummarizer::s_dialInReferenceLoaded = false;

// Static cache for cross-profile reference content (skipCatalog sections)
QString ShotSummarizer::s_crossProfileReference;
bool ShotSummarizer::s_crossProfileReferenceLoaded = false;

// Normalize a profile key: lowercase, strip diacritics, normalize punctuation.
// This is logic (retained from the markdown era) — the resolver still
// normalizes before the exact alias→id lookup so "D-Flow / Q - Jeff"
// case/accents are handled, but there is NO fuzzy fallback after a miss.
static QString normalizeProfileKey(const QString& key)
{
    QString normalized = key.toLower().trimmed();
    normalized.replace(QChar(0x00E9), 'e');  // é
    normalized.replace(QChar(0x00E8), 'e');  // è
    normalized.replace(QChar(0x00EA), 'e');  // ê
    normalized.replace(QChar(0x00EB), 'e');  // ë
    normalized.replace(QStringLiteral(" & "), QStringLiteral(" and "));
    return normalized;
}

// Build a ShotAnalysis::ExpertBand from a JSON expertBand object, or
// std::nullopt when absent/ill-formed. The validator (tools/validate_kb.py)
// is the build-time gate, but this is the LAST line of defense and does NOT
// trust it: the ExpertBand factories enforce lo<hi / positivity via Q_ASSERT
// only, which is compiled out under QT_NO_DEBUG — so a malformed-but-
// unrejected shape (non-numeric lo/hi → QJsonValue::toDouble()==0.0; hi<=lo;
// non-positive bound) would otherwise build a *wrong* live band in release
// rather than no-op. Every invalid shape here degrades to "no band" (strict
// no-op, D6) and is logged loudly (keeps the loud/silent split honest — a
// schema-valid-but-unhandled shape is a real authoring issue, not silence).
static std::optional<ShotAnalysis::ExpertBand>
expertBandFromJson(const QString& whoFor, const QJsonObject& eb)
{
    using EB = ShotAnalysis::ExpertBand;
    if (eb.isEmpty()) return std::nullopt;

    auto reject = [&](const char* why) -> std::optional<EB> {
        qWarning() << "ShotSummarizer: expertBand dropped for" << whoFor
                   << "—" << why << "(degraded to no band)";
        return std::nullopt;
    };

    const QString axis = eb.value(QStringLiteral("axis")).toString();
    const QString conf = eb.value(QStringLiteral("confidence")).toString();
    QString src = eb.value(QStringLiteral("src")).toString();
    // provenance=inferred carries no src; the factories require a non-empty
    // provenance string. Keep it honest rather than fabricated.
    if (src.isEmpty())
        src = eb.value(QStringLiteral("provenance")).toString(QStringLiteral("inferred"));

    const bool hasLo = eb.contains(QStringLiteral("lo"));
    const bool hasHi = eb.contains(QStringLiteral("hi"));
    // toDouble() yields 0.0 for a string/bool/null — only trust real numbers.
    if (hasLo && !eb.value(QStringLiteral("lo")).isDouble())
        return reject("lo is not numeric");
    if (hasHi && !eb.value(QStringLiteral("hi")).isDouble())
        return reject("hi is not numeric");
    const double lo = eb.value(QStringLiteral("lo")).toDouble();
    const double hi = eb.value(QStringLiteral("hi")).toDouble();

    if (axis == QStringLiteral("pressurePeak")) {
        if (!(hasLo && hasHi))      return reject("pressurePeak needs lo and hi");
        if (!(lo > 0.0 && lo < hi)) return reject("pressure band requires 0 < lo < hi");
        return EB::pressureBand(lo, hi, src, conf);
    }
    if (axis == QStringLiteral("extractionFlow")) {
        if (hasLo && hasHi) {
            if (!(lo >= 0.0 && lo < hi)) return reject("flow band requires 0 <= lo < hi");
            return EB::flowBand(lo, hi, src, conf);
        }
        if (hasLo) {
            if (!(lo > 0.0)) return reject("flow floor requires lo > 0");
            return EB::flowFloor(lo, src, conf);
        }
        return reject("extractionFlow needs lo (and optionally hi)");
    }
    return reject("unknown expertBand.axis");
}

void ShotSummarizer::loadProfileKnowledge()
{
    if (s_knowledgeLoaded) return;
    static QMutex mutex;
    QMutexLocker locker(&mutex);
    if (s_knowledgeLoaded) return;  // re-check after acquiring lock

    QFile file(QStringLiteral(":/ai/profile_knowledge.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "ShotSummarizer: Failed to load profile knowledge resource";
        // Latch even on failure: the resource won't reappear within a
        // process lifetime; a per-call retry in test binaries (which may
        // not link the qrc) is noise. Empty KB → every consumer no-ops.
        s_knowledgeLoaded = true;
        return;
    }
    const QByteArray raw = file.readAll();
    file.close();

    QJsonParseError perr {};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "ShotSummarizer: profile_knowledge.json parse error:"
                   << perr.errorString();
        s_knowledgeLoaded = true;
        return;
    }
    const QJsonArray profiles = doc.object().value(QStringLiteral("profiles")).toArray();

    for (const QJsonValue& pv : profiles) {
        const QJsonObject po = pv.toObject();
        ProfileKnowledge pk;
        pk.id          = po.value(QStringLiteral("id")).toString();
        pk.name        = po.value(QStringLiteral("displayName")).toString();
        pk.content     = po.value(QStringLiteral("prose")).toString();
        pk.skipCatalog = po.value(QStringLiteral("skipCatalog")).toBool(false);
        pk.family      = po.value(QStringLiteral("family")).toString();

        for (const QJsonValue& f : po.value(QStringLiteral("analysisFlags")).toArray()) {
            const QString flag = f.toString().trimmed();
            if (!flag.isEmpty()) pk.analysisFlags << flag;
        }

        const QJsonValue ugsv = po.value(QStringLiteral("ugs"));
        if (ugsv.isObject()) {
            const QJsonObject u = ugsv.toObject();
            pk.ugs = u.value(QStringLiteral("value")).toDouble(
                         std::numeric_limits<double>::quiet_NaN());
            pk.ugsInferred = u.value(QStringLiteral("inferred")).toBool(false);
        }

        if (pk.id.isEmpty()) {
            qWarning() << "ShotSummarizer: KB entry with empty id — skipped";
            continue;
        }
        pk.expertBand = expertBandFromJson(
            pk.id, po.value(QStringLiteral("expertBand")).toObject());

        // Runtime backstop — self-defending if the build-time validator gate
        // was skipped (CMake degrades to FATAL_ERROR when Python is absent,
        // but a hand-run/IDE build could still bypass it): a duplicate id or
        // an alias re-pointing to a *different* id is loud, never a silent
        // QMap-overwrite that mis-resolves a real profile.
        if (s_profileKnowledge.contains(pk.id))
            qWarning() << "ShotSummarizer: duplicate KB id" << pk.id
                       << "— later entry overwrites earlier (validator gate"
                          " should have rejected this)";
        s_profileKnowledge.insert(pk.id, pk);

        // Alias map: displayName + every alsoMatches entry + the editor-type
        // default → id. Exact (normalized) keys only; a miss is unresolved,
        // never a guess. Same normalization as the resolver (so the build
        // validator and runtime agree on what "collides").
        auto registerAlias = [&](const QString& raw) {
            const QString key = normalizeProfileKey(raw);
            const auto it = s_aliasToId.constFind(key);
            if (it != s_aliasToId.constEnd() && it.value() != pk.id)
                qWarning() << "ShotSummarizer: alias collision —" << raw
                           << "maps to both" << it.value() << "and" << pk.id
                           << "(validator gate should have rejected this)";
            s_aliasToId.insert(key, pk.id);
        };
        registerAlias(pk.name);
        for (const QJsonValue& a : po.value(QStringLiteral("alsoMatches")).toArray()) {
            const QString alias = a.toString().trimmed();
            if (!alias.isEmpty()) registerAlias(alias);
        }
        const QString edt = po.value(QStringLiteral("defaultForEditorType")).toString();
        if (edt == QStringLiteral("dflow") || edt == QStringLiteral("aflow"))
            registerAlias(QStringLiteral("__editor_default__:") + edt);
    }

    qDebug() << "ShotSummarizer: Loaded" << s_profileKnowledge.size()
             << "profile knowledge entries (" << s_aliasToId.size()
             << "alias keys )";

    buildProfileCatalog();
    s_knowledgeLoaded = true;
}

void ShotSummarizer::buildProfileCatalog()
{
    // One compact line per KB profile for cross-profile awareness:
    //   "<displayName> [family: <family>]"
    // (Replaces the former Category:/Roast: prose-scrape — those are no
    // longer fields; `family` is the load-bearing cluster tag the advisor
    // builds switching-recommendation logic on.)
    QStringList lines;
    for (auto it = s_profileKnowledge.constBegin(); it != s_profileKnowledge.constEnd(); ++it) {
        const ProfileKnowledge& pk = it.value();
        if (pk.skipCatalog) continue;  // cross-cutting reference, not a profile
        QString entry = pk.name;
        if (!pk.family.isEmpty())
            entry += QStringLiteral(" [family: ") + pk.family + QStringLiteral("]");
        lines << entry;
    }
    lines.sort(Qt::CaseInsensitive);
    s_profileCatalog = lines.join('\n');

    qDebug() << "ShotSummarizer: Built profile catalog with" << lines.size() << "entries";
}

QString ShotSummarizer::crossProfileReferenceContent()
{
    if (s_crossProfileReferenceLoaded) return s_crossProfileReference;

    loadProfileKnowledge();

    QStringList sections;
    for (auto it = s_profileKnowledge.constBegin(); it != s_profileKnowledge.constEnd(); ++it) {
        const ProfileKnowledge& pk = it.value();
        if (!pk.skipCatalog) continue;
        sections << QStringLiteral("## ") + pk.name + QStringLiteral("\n\n") + pk.content;
    }
    s_crossProfileReference = sections.join(QStringLiteral("\n\n"));
    s_crossProfileReferenceLoaded = true;
    return s_crossProfileReference;
}

void ShotSummarizer::loadDialInReference()
{
    if (s_dialInReferenceLoaded) return;

    QFile file(QStringLiteral(":/ai/espresso_dial_in_reference.md"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "ShotSummarizer: Failed to load dial-in reference resource";
        s_dialInReferenceLoaded = true;
        return;
    }
    s_dialInReferenceLoaded = true;

    QString content = QTextStream(&file).readAll();
    file.close();

    qsizetype pos = content.indexOf(QStringLiteral("\n---\n"));
    if (pos > 0)
        content = content.mid(pos + 5).trimmed();

    s_dialInReference = content;
    qDebug() << "ShotSummarizer: Loaded dial-in reference tables ("
             << s_dialInReference.size() << "chars)";
}

// Resolve any caller-supplied kbId to a canonical `id`. Accepts BOTH a
// current `id` AND a legacy normalized title/alias (shot records persist
// the old normalized-title kbId; D14a). Exact-match-or-unresolved: an
// unknown value returns "" (→ every consumer no-ops; never a fuzzy guess).
// Member (not a free function) — touches the private s_* statics.
QString ShotSummarizer::resolveKbInput(const QString& kbId)
{
    if (kbId.isEmpty()) return QString();
    if (s_profileKnowledge.contains(kbId)) return kbId;          // already an id
    return s_aliasToId.value(normalizeProfileKey(kbId));         // legacy title → id, or ""
}

// Shared matching logic: returns the resolved `id`, or empty string.
// profileTitle: the profile's display name (e.g. "D-Flow / my recipe").
// editorTypeHint: raw editorType ("dflow"/"aflow") or the profileType
//   description string ("D-Flow (lever-style...)") — both handled.
QString ShotSummarizer::matchProfileKey(const QMap<QString, ShotSummarizer::ProfileKnowledge>& /*knowledge*/,
                                        const QString& profileTitle, const QString& editorTypeHint)
{
    // Title path: normalize → EXACT alias→id lookup. No prefix/contains
    // scan (that order-dependent fallback silently mis-resolved real
    // profiles — deleted, never reintroduced).
    if (!profileTitle.isEmpty()) {
        const QString id = s_aliasToId.value(normalizeProfileKey(profileTitle));
        if (!id.isEmpty()) return id;
    }

    // Fallback: editor-type default (user-created D-Flow/A-Flow profiles
    // with fully custom titles). Resolves to the entry whose
    // defaultForEditorType matches — kept as data, not a hardcoded map.
    if (!editorTypeHint.isEmpty()) {
        QString et;
        if (editorTypeHint.startsWith(QStringLiteral("dflow"), Qt::CaseInsensitive) ||
            editorTypeHint.startsWith(QStringLiteral("D-Flow"), Qt::CaseInsensitive))
            et = QStringLiteral("dflow");
        else if (editorTypeHint.startsWith(QStringLiteral("aflow"), Qt::CaseInsensitive) ||
                 editorTypeHint.startsWith(QStringLiteral("A-Flow"), Qt::CaseInsensitive))
            et = QStringLiteral("aflow");
        if (!et.isEmpty()) {
            const QString synthetic = normalizeProfileKey(
                QStringLiteral("__editor_default__:") + et);
            const QString id = s_aliasToId.value(synthetic);
            if (!id.isEmpty()) return id;
        }
    }
    return QString();
}

QString ShotSummarizer::findProfileSection(const QString& profileTitle, const QString& profileType)
{
    if (profileTitle.isEmpty() && profileType.isEmpty()) return QString();
    loadProfileKnowledge();
    const QString id = matchProfileKey(s_profileKnowledge, profileTitle, profileType);
    return id.isEmpty() ? QString() : s_profileKnowledge.value(id).content;
}

QString ShotSummarizer::profileKnowledgeForKbId(const QString& profileKbId)
{
    if (profileKbId.isEmpty()) return QString();
    loadProfileKnowledge();
    const QString id = resolveKbInput(profileKbId);
    return id.isEmpty() ? QString() : s_profileKnowledge.value(id).content;
}

QStringList ShotSummarizer::getAnalysisFlags(const QString& kbId)
{
    if (kbId.isEmpty()) return {};
    loadProfileKnowledge();
    const QString id = resolveKbInput(kbId);
    return id.isEmpty() ? QStringList() : s_profileKnowledge.value(id).analysisFlags;
}

QString ShotSummarizer::computeProfileKbId(const QString& profileTitle, const QString& editorType)
{
    if (profileTitle.isEmpty() && editorType.isEmpty()) return QString();
    loadProfileKnowledge();
    return matchProfileKey(s_profileKnowledge, profileTitle, editorType);
}

double ShotSummarizer::ugsForKbId(const QString& kbId)
{
    if (kbId.isEmpty()) return std::numeric_limits<double>::quiet_NaN();
    loadProfileKnowledge();
    const QString id = resolveKbInput(kbId);
    return id.isEmpty() ? std::numeric_limits<double>::quiet_NaN()
                        : s_profileKnowledge.value(id).ugs;
}

bool ShotSummarizer::ugsInferredForKbId(const QString& kbId)
{
    if (kbId.isEmpty()) return false;
    loadProfileKnowledge();
    const QString id = resolveKbInput(kbId);
    return id.isEmpty() ? false : s_profileKnowledge.value(id).ugsInferred;
}

QString ShotSummarizer::canonicalNameForKbId(const QString& kbId)
{
    if (kbId.isEmpty()) return QString();
    loadProfileKnowledge();
    const QString id = resolveKbInput(kbId);
    return id.isEmpty() ? QString() : s_profileKnowledge.value(id).name;
}

std::optional<ShotAnalysis::ExpertBand>
ShotSummarizer::expertBandForKbId(const QString& kbId)
{
    // Band lives in the KB entry (the hardcoded C++ kBands table is gone —
    // single source of truth). Resolved fresh every call from the qrc
    // resource shipped with the binary (recompute-on-load contract).
    // Absence → std::nullopt → strict no-op, byte-identical to the
    // shipped absence-intentional behavior (D6).
    if (kbId.isEmpty()) return std::nullopt;
    loadProfileKnowledge();
    const QString id = resolveKbInput(kbId);
    if (id.isEmpty()) return std::nullopt;
    return s_profileKnowledge.value(id).expertBand;
}

QList<ShotSummarizer::KbUgsEntry> ShotSummarizer::allKbUgsEntries()
{
    loadProfileKnowledge();

    // s_profileKnowledge is keyed by `id` (one entry per canonical
    // identity), so iteration is already deduplicated — no by-name pass.
    QList<KbUgsEntry> out;
    for (auto it = s_profileKnowledge.constBegin(); it != s_profileKnowledge.constEnd(); ++it) {
        const ProfileKnowledge& pk = it.value();
        if (std::isnan(pk.ugs)) continue;
        KbUgsEntry e;
        e.kbId = pk.id;
        e.name = pk.name;
        e.ugs = pk.ugs;
        e.ugsInferred = pk.ugsInferred;
        out << e;
    }
    return out;
}
