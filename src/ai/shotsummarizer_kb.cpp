// Profile-knowledge resolution cluster — extracted verbatim from
// shotsummarizer.cpp (change: flag-off-expert-band-in-shot-summary).
//
// WHY A SEPARATE TU: these `ShotSummarizer::` statics (loadProfileKnowledge,
// matchProfileKey, computeProfileKbId, getAnalysisFlags, ugs*, canonical
// NameForKbId, expertBandForKbId, allKbUgsEntries, buildProfileCatalog,
// crossProfileReferenceContent, …) form a cohesive KB-data layer that
// depends only on the `:/ai/*.md` resources + QString — NOT on the
// prompt/summary/dialing machinery the rest of shotsummarizer.cpp pulls
// (shotdatamodel, visualizeruploader [Qt Network], dialing_blocks). Keeping
// them in the monolith forced any offline consumer (the `shot_eval` tool,
// `tst_shotrecord_cache`) to drag the whole app in. Split out so those
// consumers link a lean closure ({this TU} + ai.qrc) and the tool can route
// through the SAME prepareAnalysisInputs → analyzeShot → deriveBadges path
// as the app (parity by construction). Declarations stay in
// shotsummarizer.h as `ShotSummarizer::` statics — zero API/caller change;
// the class-static definitions (s_profileKnowledge et al.) live here, one
// definition, referenced by both TUs.
#include "shotsummarizer.h"
#include "shotanalysis.h"  // ShotAnalysis::ExpertBand (expertBandForKbId return)

#include <cmath>
#include <limits>
#include <QFile>
#include <QTextStream>
#include <QStringList>
#include <QSet>
#include <QMap>
#include <QRegularExpression>
#include <QMutex>
#include <QMutexLocker>
#include <QDebug>

// Static members for profile knowledge cache
QMap<QString, ShotSummarizer::ProfileKnowledge> ShotSummarizer::s_profileKnowledge;
bool ShotSummarizer::s_knowledgeLoaded = false;

// Static cache for profile catalog (compact one-liner per KB profile)
QString ShotSummarizer::s_profileCatalog;

// Static cache for dial-in reference tables
QString ShotSummarizer::s_dialInReference;
bool ShotSummarizer::s_dialInReferenceLoaded = false;

// Static cache for cross-profile reference content (Skip-Catalog sections)
QString ShotSummarizer::s_crossProfileReference;
bool ShotSummarizer::s_crossProfileReferenceLoaded = false;

// Normalize a profile key: lowercase, strip diacritics, normalize punctuation
static QString normalizeProfileKey(const QString& key)
{
    QString normalized = key.toLower().trimmed();
    // Normalize common diacritics (é→e, è→e, ê→e, ë→e, etc.)
    normalized.replace(QChar(0x00E9), 'e');  // é
    normalized.replace(QChar(0x00E8), 'e');  // è
    normalized.replace(QChar(0x00EA), 'e');  // ê
    normalized.replace(QChar(0x00EB), 'e');  // ë
    // Normalize & ↔ and
    normalized.replace(QStringLiteral(" & "), QStringLiteral(" and "));
    return normalized;
}

void ShotSummarizer::loadProfileKnowledge()
{
    if (s_knowledgeLoaded) return;
    static QMutex mutex;
    QMutexLocker locker(&mutex);
    if (s_knowledgeLoaded) return;  // re-check after acquiring lock

    QFile file(QStringLiteral(":/ai/profile_knowledge.md"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "ShotSummarizer: Failed to load profile knowledge resource";
        // Latch the loaded flag even on failure so subsequent calls
        // don't re-warn — the resource won't reappear inside one
        // process's lifetime, and a per-call retry in test binaries
        // (which don't link the qrc) creates noise without value.
        s_knowledgeLoaded = true;
        return;
    }

    QString content = QTextStream(&file).readAll();
    file.close();

    // Parse markdown sections: each "## Title" starts a new profile
    // Build a map from lowercase key → ProfileKnowledge
    const QStringList lines = content.split('\n');
    QString currentTitle;
    QString currentContent;

    auto commitSection = [&]() {
        if (currentTitle.isEmpty() || currentContent.isEmpty()) return;

        ProfileKnowledge pk;
        pk.name = currentTitle;

        // Strip parser-directive lines that are not useful to the AI
        {
            QStringList filtered;
            for (const QString& l : currentContent.split('\n')) {
                if (!l.startsWith(QStringLiteral("Skip-Catalog:")) &&
                    !l.startsWith(QStringLiteral("Purpose:")))
                    filtered << l;
            }
            pk.content = filtered.join('\n').trimmed();
        }

        // Extract the main name and any aliases from "Also matches:" line
        QStringList keys;

        // Primary title may have " / " separators (e.g. "D-Flow / Damian's D-Flow / D-Flow/Q")
        const QStringList titleParts = currentTitle.split(QStringLiteral(" / "));
        for (const QString& part : titleParts) {
            keys << part.trimmed().toLower();
        }

        // Check for "Also matches:" and "AnalysisFlags:" lines in content
        for (const QString& line : currentContent.split('\n')) {
            if (line.startsWith(QStringLiteral("Also matches:"))) {
                QString aliases = line.mid(14).trimmed();
                // Remove surrounding quotes and split by comma
                const QStringList aliasParts = aliases.split(',');
                for (const QString& alias : aliasParts) {
                    QString clean = alias.trimmed();
                    clean.remove('"');
                    if (!clean.isEmpty()) {
                        keys << clean.toLower();
                    }
                }
            } else if (line.startsWith(QStringLiteral("AnalysisFlags:"))) {
                const QString flagStr = line.mid(14).trimmed();
                for (const QString& f : flagStr.split(',')) {
                    const QString flag = f.trimmed();
                    if (!flag.isEmpty()) pk.analysisFlags << flag;
                }
            } else if (line.startsWith(QStringLiteral("Skip-Catalog:"))) {
                pk.skipCatalog = (line.mid(13).trimmed().toLower() == QStringLiteral("true"));
            } else if (line.startsWith(QStringLiteral("UGS:"))) {
                QString val = line.mid(4).trimmed();
                bool inferredMarker = false;
                if (val.startsWith('~')) {
                    inferredMarker = true;
                    val = val.mid(1);
                }
                // Strip parenthetical annotation and everything after it
                const int parenIdx = val.indexOf('(');
                if (parenIdx >= 0)
                    val = val.left(parenIdx);
                val = val.trimmed();
                bool ok = false;
                const double parsed = val.toDouble(&ok);
                if (ok) {
                    pk.ugs = parsed;
                    pk.ugsInferred = inferredMarker;
                } else if (!val.isEmpty()) {
                    qWarning() << "ShotSummarizer: UGS parse failed for profile"
                               << pk.name << "— value:" << val;
                }
                // On failure: leave ugs=NaN, ugsInferred=false — identical to missing line.
            }
        }

        // Register all keys (normalized for accent/punctuation matching)
        for (const QString& key : keys) {
            s_profileKnowledge.insert(normalizeProfileKey(key), pk);
        }
    };

    for (const QString& line : lines) {
        if (line.startsWith(QStringLiteral("## ")) && !line.startsWith(QStringLiteral("### "))) {
            // Commit previous section
            commitSection();
            currentTitle = line.mid(3).trimmed();
            currentContent.clear();
        } else if (!currentTitle.isEmpty()) {
            currentContent += line + '\n';
        }
    }
    // Commit last section
    commitSection();

    qDebug() << "ShotSummarizer: Loaded" << s_profileKnowledge.size()
             << "profile knowledge entries";

    buildProfileCatalog();

    // Mark loaded only after parse + catalog build complete. The
    // double-checked-locking pattern at the top of this function depends
    // on the flag indicating "data ready to read", not "we got far enough
    // to open the file". Setting it earlier let a second thread that won
    // the outer race read an empty s_profileKnowledge.
    s_knowledgeLoaded = true;
}

void ShotSummarizer::buildProfileCatalog()
{
    // Build compact one-liner per unique KB profile for cross-profile awareness.
    // Extracts Category and Roast lines from each profile's content.
    QSet<QString> seen;
    QStringList lines;

    for (auto it = s_profileKnowledge.constBegin(); it != s_profileKnowledge.constEnd(); ++it) {
        const ProfileKnowledge& pk = it.value();
        if (pk.skipCatalog) continue;  // cross-cutting reference, not a profile
        if (seen.contains(pk.name)) continue;
        seen.insert(pk.name);

        QString category;
        QString family;
        QString roast;
        for (const QString& line : pk.content.split('\n')) {
            if (line.startsWith(QStringLiteral("Category:")) && category.isEmpty()) {
                category = line.mid(9).trimmed();
            } else if (line.startsWith(QStringLiteral("Family:")) && family.isEmpty()) {
                family = line.mid(7).trimmed();
            } else if (line.startsWith(QStringLiteral("Roast:")) && roast.isEmpty()) {
                roast = line.mid(6).trimmed();
            }
        }

        QString entry = pk.name + QStringLiteral(" — ") + category;
        if (!family.isEmpty()) {
            entry += QStringLiteral(" [family: ") + family + QStringLiteral("]");
        }
        if (!roast.isEmpty()) {
            entry += QStringLiteral(". ") + roast;
        }
        lines << entry;
    }

    // Sort alphabetically for consistent ordering
    lines.sort(Qt::CaseInsensitive);
    s_profileCatalog = lines.join('\n');

    qDebug() << "ShotSummarizer: Built profile catalog with" << lines.size() << "entries";
}

QString ShotSummarizer::crossProfileReferenceContent()
{
    if (s_crossProfileReferenceLoaded) return s_crossProfileReference;

    loadProfileKnowledge();

    QSet<QString> seen;
    QStringList sections;
    for (auto it = s_profileKnowledge.constBegin(); it != s_profileKnowledge.constEnd(); ++it) {
        const ProfileKnowledge& pk = it.value();
        if (!pk.skipCatalog) continue;
        if (seen.contains(pk.name)) continue;
        seen.insert(pk.name);
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

    // Strip the preamble (title, source attribution, description) — already introduced
    // by the section header in shotAnalysisSystemPrompt(). Seek to the first HR separator.
    qsizetype pos = content.indexOf(QStringLiteral("\n---\n"));
    if (pos > 0)
        content = content.mid(pos + 5).trimmed();  // skip past "\n---\n"

    s_dialInReference = content;
    qDebug() << "ShotSummarizer: Loaded dial-in reference tables ("
             << s_dialInReference.size() << "chars)";
}

// Shared matching logic: returns the matched key in s_profileKnowledge, or empty string.
// profileTitle: the profile's display name (e.g. "D-Flow / my recipe")
// editorTypeHint: either the raw editorType string ("dflow", "aflow") or the
//   profileType description string ("D-Flow (lever-style...)") — both are handled.
QString ShotSummarizer::matchProfileKey(const QMap<QString, ShotSummarizer::ProfileKnowledge>& knowledge,
                                        const QString& profileTitle, const QString& editorTypeHint)
{
    if (knowledge.isEmpty()) return QString();

    // Try title-based matching first
    if (!profileTitle.isEmpty()) {
        QString key = normalizeProfileKey(profileTitle);

        // Direct match
        if (knowledge.contains(key)) {
            return key;
        }

        // Try without version suffixes (e.g., "Adaptive v2.1" → "adaptive v2")
        // Try progressively shorter prefixes
        for (const auto& knownKey : knowledge.keys()) {
            if (key.startsWith(knownKey) || knownKey.startsWith(key)) {
                return knownKey;
            }
        }

        // Fuzzy: check if any known key is contained within the profile title
        for (const auto& knownKey : knowledge.keys()) {
            if (knownKey.length() >= 4 && key.contains(knownKey)) {
                return knownKey;
            }
        }
    }

    // Fallback: match by editor type
    // This handles user-created profiles from the D-Flow/A-Flow editors
    // that may have completely custom titles
    if (!editorTypeHint.isEmpty()) {
        // Map raw editorType strings and description prefixes to knowledge base keys
        static const QMap<QString, QString> editorTypeToKey = {
            { QStringLiteral("dflow"),  QStringLiteral("d-flow / default") },
            { QStringLiteral("aflow"),  QStringLiteral("a-flow") },
            { QStringLiteral("D-Flow"), QStringLiteral("d-flow / default") },
            { QStringLiteral("A-Flow"), QStringLiteral("a-flow") },
        };

        // Try exact match first (raw editorType like "dflow")
        if (editorTypeToKey.contains(editorTypeHint)) {
            const QString& mapped = editorTypeToKey.value(editorTypeHint);
            if (knowledge.contains(mapped)) return mapped;
        }

        // Try prefix match (description string like "D-Flow (lever-style...)")
        for (auto it = editorTypeToKey.constBegin(); it != editorTypeToKey.constEnd(); ++it) {
            if (editorTypeHint.startsWith(it.key())) {
                if (knowledge.contains(it.value())) {
                    return it.value();
                }
            }
        }
    }

    return QString();
}

QString ShotSummarizer::findProfileSection(const QString& profileTitle, const QString& profileType)
{
    if (profileTitle.isEmpty() && profileType.isEmpty()) return QString();

    loadProfileKnowledge();

    QString key = matchProfileKey(s_profileKnowledge, profileTitle, profileType);
    if (!key.isEmpty()) {
        return s_profileKnowledge.value(key).content;
    }
    return QString();
}

QString ShotSummarizer::profileKnowledgeForKbId(const QString& profileKbId)
{
    if (profileKbId.isEmpty()) return QString();
    loadProfileKnowledge();
    if (s_profileKnowledge.contains(profileKbId))
        return s_profileKnowledge.value(profileKbId).content;
    return QString();
}

QStringList ShotSummarizer::getAnalysisFlags(const QString& kbId)
{
    if (kbId.isEmpty()) return {};
    loadProfileKnowledge();
    return s_profileKnowledge.value(kbId).analysisFlags;
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
    return s_profileKnowledge.value(kbId).ugs;
}

bool ShotSummarizer::ugsInferredForKbId(const QString& kbId)
{
    if (kbId.isEmpty()) return false;
    loadProfileKnowledge();
    return s_profileKnowledge.value(kbId).ugsInferred;
}

QString ShotSummarizer::canonicalNameForKbId(const QString& kbId)
{
    if (kbId.isEmpty()) return QString();
    loadProfileKnowledge();
    return s_profileKnowledge.value(kbId).name;
}

std::optional<ShotAnalysis::ExpertBand>
ShotSummarizer::expertBandForKbId(const QString& kbId)
{
    using ExpertBand = ShotAnalysis::ExpertBand;
    // Citation-bound table keyed by *canonical KB-section identity*
    // (pk.name), seeded ONLY where capture-dialin-coaching-guidance design
    // D9/D10/D10b grades a cited band. Phase A (change: flag-off-expert-
    // band-in-shot-summary) seeds only the gold pair; Phases B/C add rows.
    // A canonical name with no row → absent (the check no-ops). Never
    // fabricate a row to "complete" a profile — absence is intentional.
    static const QHash<QString, ExpertBand> kBands = {
        // D-Flow / Q ≡ Damian's Q both alias `## D-Flow Q variant`, so
        // both resolve to this one canonical entry (structural zero-dup).
        // Profile-notes verbatim: "grind for a pressure peak between 6 and
        // 9 bar". Editor pressure limit 10.0 > 9 → both sides unconfounded.
        { QStringLiteral("D-Flow Q variant"),
          ExpertBand::pressureBand(6.0, 9.0,
            QStringLiteral("[SRC:profile-notes]"), QStringLiteral("high")) },
        // D-Flow / La Pavoni — its own `## D-Flow La Pavoni variant`
        // section (split out by #1175). Same profile-notes verbatim 6–9
        // bar goal. (Editor limit 9.0 == band ceiling is the firmware
        // limiter, only ever a corroborating clause — D1 — never the band.)
        { QStringLiteral("D-Flow La Pavoni variant"),
          ExpertBand::pressureBand(6.0, 9.0,
            QStringLiteral("[SRC:profile-notes]"), QStringLiteral("high")) },
        // Phase B — A-Flow family. All shipped A-Flow-editor profiles
        // canonical-key to the single `## A-Flow` KB section, so one row
        // covers them (same structural dedup as the gold pair). Cited band
        // is the A-Flow repo's editor-level dial-in guidance step 1,
        // verbatim: "grind fine enough to reach a pressure peak between 6
        // and 9 bar at extraction" [SRC:aflow-repo]
        // (docs/PROFILE_KNOWLEDGE_BASE.md:240 — the design doc; the runtime
        // qrc resource is the lower-case profile_knowledge.md, which only
        // carries the summary). Confidence `medium`: editor-level guidance
        // spanning all roasts (vs the gold pair's profile-notes `high`).
        // Validated against the real community A-Flow / default-medium
        // population (20 shots, 4 users): the band partitions cleanly —
        // SILENT 6–9 (on Janek's target, off the limiter), FIRE >9 (grind
        // too fine → on default-medium that pegs its 10-bar Flow-Extraction
        // limiter; other shipped A-Flow variants' limiters range 9.0–10.0
        // bar, but the band fires on the peak regardless of each profile's
        // limiter value — the bad regime), FIRE <6 (too coarse). The
        // limiter peg (default-medium) is not a rival rail; it is the
        // mechanism that corroborates why >9 is bad — exactly D1 (limiter
        // corroborates, band is primary). Earlier "STOP" rested on
        // trusting one lenient rater's scores to call limiter-pegged
        // shots good; corrected here.
        { QStringLiteral("A-Flow"),
          ExpertBand::pressureBand(6.0, 9.0,
            QStringLiteral("[SRC:aflow-repo]"), QStringLiteral("medium")) },
        // Phase C — Londinium (the standalone `## Londinium` section).
        // Damian's LRv2/LRv3 never pick up this band because they resolve
        // to their OWN canonical KB section (`Damian's LRv2 / LRv3`) which
        // has no kBands row — NOT because of the `## Londinium` Note (that
        // Note is LLM-only prose, inert for C++ key resolution). Guarded by
        // tst_dialing_blocks::expertBand_londinium_resolvesAndDoesNotCatchDamianLR.
        // Cited band: the official Decent dial-in guide —
        // "9 bar peak with gradual decline; too fine: pressure over 9 bar;
        // too coarse: pressure crash" [SRC:decent-guide]
        // (docs/PROFILE_KNOWLEDGE_BASE.md:361 — the design doc; the runtime
        // qrc resource is the lower-case profile_knowledge.md, which only
        // carries the summary). Non-adaptive (unlike
        // Adaptive v2, which was gate-failed and left absent — its KB
        // section forbids flagging its by-design variable pressure). The
        // 3-bar soak is intentional pre-infusion; the pour-window peak
        // measure excludes it (the peak is the ~9-bar extraction). Gate:
        // 20-shot / 5-user community population clusters at the cited
        // target (extraction-peak median 9.0, mode 9) and band [8,9]
        // partitions verbatim onto the guide — SILENT 8–9 (on target),
        // FIRE >9 (cited "too fine: pressure over 9 bar"), FIRE <8 (cited
        // "too coarse: pressure crash"). Confidence `medium` (Phase-C
        // contextual tail; same rung as A-Flow's editor guidance).
        { QStringLiteral("Londinium"),
          ExpertBand::pressureBand(8.0, 9.0,
            QStringLiteral("[SRC:decent-guide]"), QStringLiteral("medium")) },
    };
    if (kbId.isEmpty()) return std::nullopt;
    loadProfileKnowledge();
    const QString canonical = s_profileKnowledge.value(kbId).name;
    if (canonical.isEmpty()) return std::nullopt;
    const auto it = kBands.constFind(canonical);
    return it == kBands.constEnd() ? std::nullopt
                                   : std::optional<ExpertBand>(*it);
}

QList<ShotSummarizer::KbUgsEntry> ShotSummarizer::allKbUgsEntries()
{
    loadProfileKnowledge();

    // Deduplicate by canonical name (pk.name) — multiple aliases map to the
    // same ProfileKnowledge; emit one entry per canonical name.
    QMap<QString, KbUgsEntry> byName;
    for (auto it = s_profileKnowledge.constBegin(); it != s_profileKnowledge.constEnd(); ++it) {
        const ProfileKnowledge& pk = it.value();
        if (std::isnan(pk.ugs)) continue;
        if (byName.contains(pk.name)) continue;
        KbUgsEntry e;
        e.kbId = it.key();
        e.name = pk.name;
        e.ugs = pk.ugs;
        e.ugsInferred = pk.ugsInferred;
        byName.insert(pk.name, e);
    }
    return byName.values();
}
