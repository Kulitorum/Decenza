// profile_sync.cpp — Developer tool to compare and sync built-in profile JSONs
// against de1app TCL source profiles using the same C++ parser as the app and tests.
//
// Replaces scripts/compare_profiles.py and scripts/sync_profiles.py.
//
// Usage:
//   profile_sync <de1app_profiles_dir> <builtin_profiles_dir> [--sync]
//
//   Without --sync: report all differences (compare mode)
//   With    --sync: also overwrite stale JSONs and create missing ones
//
// The first argument should be de1app's `de1plus/profiles/` directory. Plugin profile
// directories under `<first-arg>/../plugins/*/profiles/` are scanned automatically if a
// `plugins/` sibling exists; when a plugin profile shares the same output filename as a
// base profile, the plugin copy wins (canonical source). If the two copies DISAGREE the
// tool prints a SOURCE CONFLICT to stderr naming both paths and what differs — the
// precedence is still applied, but never silently (see de1app issue #350).

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QTextStream>
#include <QRegularExpression>
#include <QDebug>
#include <QHash>

#include "profile/de1apptclfields.h"
#include "profile/profile.h"
#include "profile/profileframe.h"

// Materialize frames for simple profiles (settings_2a/2b) loaded from JSON.
// Built-in JSONs ship with `"steps": []` because the app regenerates frames at
// activation time via ProfileManager::regenerateSimpleFrames(). Profile::loadFromTclString
// does the same thing at parse time, so to compare like-for-like we have to regenerate
// here when the JSON side has empty steps. Profile::fromJson() already does this for
// shipping JSONs, but call it here defensively in case a JSON was written with
// non-empty steps and a stale preinfuseFrameCount.
static void normaliseSimpleProfile(Profile& p)
{
    const QString t = p.profileType();
    if (t != QLatin1String("settings_2a") && t != QLatin1String("settings_2b"))
        return;
    if (p.steps().isEmpty())
        p.regenerateSimpleFrames();
    // For simple profiles, NumberOfPreinfuseFrames is a derived value: de1app
    // calculates it during frame generation (pressure_to_advanced_list /
    // flow_to_advanced_list in de1plus/profile.tcl). The TCL source files still
    // carry a literal `final_desired_shot_volume_advanced_count_start` field
    // (typically 0) which the Decenza TCL parser stores verbatim — that produces
    // a spurious mismatch against the value derived from the regenerated frames.
    // Normalise both sides to the derived count so the comparison reflects what
    // the DE1 actually receives at upload time.
    p.setPreinfuseFrameCount(Profile::countPreinfuseFrames(p.steps()));
}

// Replace a built-in with its de1app source. The rewrite is deliberately a
// REPLACEMENT, not a merge: a shipped built-in is supposed to be its de1app
// profile, so keeping anything extra is how the two drift apart again. Any key
// the rewrite would drop is reported first — the audit is the check on that
// claim, and it is expected to stay silent.
static bool syncOverBuiltin(const Profile& tcl, const QString& outPath, QTextStream& cerr)
{
    QFile f(outPath);
    if (!f.open(QIODevice::ReadOnly)) {
        // The audit cannot run blind. Defaulting `existing` to {} on a read
        // failure made keysLostByRewrite() return empty and the check PASS
        // vacuously — precisely on the file where you most want it.
        cerr << "  → REFUSED: cannot read the existing built-in (" << f.errorString()
             << "); the drop audit would pass vacuously\n";
        return false;
    }
    QJsonParseError parseError;
    const QJsonObject existing = QJsonDocument::fromJson(f.readAll(), &parseError).object();
    if (parseError.error != QJsonParseError::NoError) {
        cerr << "  → REFUSED: existing built-in is not valid JSON ("
             << parseError.errorString() << " at offset " << parseError.offset << ")\n";
        return false;
    }

    const QStringList lost = De1AppTcl::keysLostByRewrite(existing, tcl.toJsonObject());
    if (!lost.isEmpty()) {
        // The header for keysLostByRewrite() says a non-empty result "is a stop,
        // not something to merge around". This used to warn and write anyway,
        // which made the contract advisory. --rewrite-format already gets this
        // right, and its comment records why: an earlier revision wrote first
        // and audited the file it had just clobbered, so by the time "DATA LOSS"
        // appeared the original existed only in git.
        cerr << "  → REFUSED (file left untouched), keys would be dropped: "
             << lost.join(QLatin1String(", ")) << "\n"
             << "     A built-in is supposed to BE its de1app profile. If these keys are\n"
             << "     genuinely obsolete, teach the reader to carry them or remove them\n"
             << "     deliberately — do not let a sync decide it silently.\n";
        return false;
    }

    return tcl.saveToFile(outPath);
}

// Read a whole file as text, empty on failure.
static QString readTextFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QTextStream(&f).readAll();
}

// Profile-level scalar drift between a raw de1app .tcl and the built-in JSON,
// rendered for the report. Frames are covered separately by
// Profile::frameDiffReport(); the two together are the comparison.
static QString buildScalarDiff(const QString& tclContent, const QString& builtinPath)
{
    QFile f(builtinPath);
    if (!f.open(QIODevice::ReadOnly))
        return QStringLiteral("  (cannot read built-in JSON)\n");
    const QJsonObject json = QJsonDocument::fromJson(f.readAll()).object();

    QString report;
    for (const De1AppTcl::ScalarDiff& d : De1AppTcl::compareScalars(tclContent, json)) {
        report += QString("  %1 (tcl %2): TCL=%3 JSON=%4\n")
                      .arg(d.canonical, d.tclKey, d.tclValue, d.jsonValue);
    }
    return report;
}

// Walk `<baseDir>/../plugins/*/profiles/*.tcl`. Empty list if no `plugins/` sibling.
static QStringList findPluginTclFiles(const QDir& baseDir)
{
    QStringList result;
    QDir parent(baseDir);
    if (!parent.cdUp()) return result;
    QDir plugins(parent.absoluteFilePath(QStringLiteral("plugins")));
    if (!plugins.exists()) return result;

    const QStringList pluginNames = plugins.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString& pluginName : pluginNames) {
        QDir pluginProfiles(plugins.absoluteFilePath(pluginName + QStringLiteral("/profiles")));
        if (!pluginProfiles.exists()) continue;
        const QStringList tcls = pluginProfiles.entryList({QLatin1String("*.tcl")}, QDir::Files, QDir::Name);
        for (const QString& f : tcls)
            result.append(pluginProfiles.absoluteFilePath(f));
    }
    return result;
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    const QStringList args = app.arguments();
    if (args.size() < 3) {
        QTextStream(stderr) << "Usage: profile_sync <de1app_profiles_dir> <builtin_profiles_dir> [--sync|--rewrite-format]\n"
                            << "\n"
                            << "  Without --sync: report differences only (compare mode)\n"
                            << "  With    --sync: also overwrite stale JSONs and create missing ones\n"
                            << "  --force:        with --sync, rewrite EVERY profile, including ones\n"
                            << "                  that read as in-sync (the frame comparison ignores\n"
                            << "                  the axis a frame's pump does not drive, so a stale\n"
                            << "                  value can hide there)\n"
                            << "  --rewrite-format: format-only pass — re-save the built-in JSONs through\n"
                            << "                    the canonical serializer, leaving content untouched\n"
                            << "                    (de1app dir is ignored; content sync is a separate task)\n"
                            << "\n"
                            << "  Plugin profiles under <de1app_profiles_dir>/../plugins/*/profiles/\n"
                            << "  are scanned automatically and override base profiles with the same\n"
                            << "  output filename (canonical source wins).\n";
        return 1;
    }

    const QString de1appDir  = args[1];
    const QString builtinDir = args[2];
    const bool    doSync     = args.contains(QLatin1String("--sync"));
    const bool    doRewrite  = args.contains(QLatin1String("--rewrite-format"));
    // --force: rewrite every profile from its .tcl, not only those the
    // comparison flags. Needed because the frame comparison deliberately
    // ignores the axis a frame's pump does not drive (the DE1 ignores it too),
    // so a built-in can hold a stale value there and still read as "in sync".
    // That is exactly how built-ins kept an inactive-axis value de1app never
    // wrote (19 of 93 carried the bare member default). Use after changing what
    // the writer emits, not only when the comparison reports drift.
    const bool    doForce    = args.contains(QLatin1String("--force"));

    // --rewrite-format: FORMAT-ONLY pass over the built-in JSONs. Loads each file
    // and re-saves it through the canonical serializer (Profile::toJsonObject), so
    // the shipped set adopts the string-encoded, Decaid-readable format without
    // touching profile CONTENT. Deliberately independent of the de1app comparison:
    // reconciling content against de1app/Decaid is a separate concern (OpenSpec
    // sync-builtin-profiles), and conflating the two would hide content changes
    // inside a format diff.
    if (doRewrite && doSync) {
        QTextStream(stderr) << "Error: --sync and --rewrite-format are mutually exclusive.\n"
                            << "  --sync           pulls de1app CONTENT into the built-ins\n"
                            << "  --rewrite-format re-saves them in the canonical format, content untouched\n";
        return 1;
    }

    if (doRewrite) {
        QDir out(builtinDir);
        if (!out.exists()) {
            QTextStream(stderr) << "Error: built-in profiles directory not found: " << builtinDir << "\n";
            return 1;
        }
        QTextStream cout(stdout);
        QTextStream cerr(stderr);   // failures go to stderr so CI/greps can see them
        int rewritten = 0, lost = 0, skipped = 0, writeFailed = 0;
        const QStringList jsons = out.entryList({QLatin1String("*.json")}, QDir::Files, QDir::Name);
        for (const QString& fileName : jsons) {
            const QString path = out.absoluteFilePath(fileName);
            // Snapshot the file EXACTLY as it sits on disk. The audit below compares
            // against this, not against a re-serialization — comparing two outputs of
            // the same serializer can only ever agree with itself.
            QJsonObject original;
            {
                QFile f(path);
                if (f.open(QIODevice::ReadOnly))
                    original = QJsonDocument::fromJson(f.readAll()).object();
            }

            Profile before = Profile::loadFromFile(path);
            if (!before.isValid() || original.isEmpty()) {
                cerr << "SKIP (invalid): " << fileName << "\n";
                ++skipped;
                continue;
            }

            // AUDIT BEFORE WRITING. The serialization happens in memory and the file
            // is only touched once parity is proven — an earlier revision wrote first
            // and audited the file it had just clobbered, so by the time "DATA LOSS"
            // appeared the original existed only in git.
            //
            // Parity is checked against the ORIGINAL BYTES ON DISK, never against a
            // re-serialization: comparing two outputs of the same serializer can only
            // ever agree with itself. functionallyEqual() is also NOT sufficient — it
            // compares frames and the preinfuse count only, and reported
            // "content-identical" while this pass was stripping recipe blocks and
            // de1app's simple-editor keys from dozens of built-ins.
            const QJsonObject candidate = before.toJsonObject();
            const QStringList parity = Profile::jsonParityErrors(original, candidate);
            if (!parity.isEmpty()) {
                cerr << "DATA LOSS (file left untouched): " << fileName << "\n";
                for (const QString& e : parity) cerr << "    " << e << "\n";
                ++lost;
                continue;
            }
            // Also refuse to write something a stricter reader in the ecosystem
            // would reject outright — the whole point of the canonical format.
            const QStringList readability = Profile::decaidReadabilityErrors(candidate);
            if (!readability.isEmpty()) {
                cerr << "NOT READABLE (file left untouched): " << fileName << "\n";
                for (const QString& e : readability) cerr << "    " << e << "\n";
                ++lost;
                continue;
            }

            if (!before.saveToFile(path)) {
                cerr << "ERROR (write failed): " << fileName << "\n";
                ++writeFailed;
                continue;
            }
            ++rewritten;
        }
        // Separate counters: an earlier revision folded data-loss, invalid and
        // write-error into one "skipped" total while ALSO counting the lossy file as
        // rewritten — so a destructive run read as "the tool skipped a few odd ones".
        cout << "\nFormat rewrite complete: " << rewritten << " rewritten\n";
        if (lost)        cerr << "  " << lost        << " REFUSED (data loss / unreadable) — left untouched\n";
        if (skipped)     cerr << "  " << skipped     << " skipped (unparseable)\n";
        if (writeFailed) cerr << "  " << writeFailed << " write errors\n";
        return (lost || skipped || writeFailed) ? 1 : 0;
    }

    QDir src(de1appDir);
    if (!src.exists()) {
        QTextStream(stderr) << "Error: de1app profiles directory not found: " << de1appDir << "\n";
        return 1;
    }
    QDir out(builtinDir);
    if (!out.exists()) {
        QTextStream(stderr) << "Error: built-in profiles directory not found: " << builtinDir << "\n";
        return 1;
    }

    QTextStream cout(stdout);
    QTextStream cerr(stderr);   // failures go to stderr so CI/greps can see them

    // Build the unified TCL source list. Base files come first, then plugin files
    // override base entries when the resulting filename collides — that way a
    // canonical 9-frame plugin A-Flow profile beats the stale 6-frame base copy.
    struct Source {
        QString tclPath;       // absolute path to the TCL source
        QString tclContent;    // raw text — the scalar comparison reads it directly
        QString outFilename;   // Profile::titleToFilename(title) + ".json"
        Profile profile;       // already-parsed (avoid re-parsing later)
        bool fromPlugin = false;
    };
    QHash<QString, Source> sources;          // key: outFilename
    QHash<QString, QString> overriddenBy;    // outFilename -> plugin path (for reporting)
    int parseFailed = 0;
    int sourceConflicts = 0;                 // plugin and base copies that disagree

    auto ingest = [&](const QString& tclPath, bool fromPlugin) {
        const QString content = readTextFile(tclPath);
        if (content.isEmpty()) {
            cerr << "SKIP (cannot read): " << tclPath << "\n";
            ++parseFailed;
            return;
        }
        const Profile tcl = Profile::loadFromTclString(content);
        if (tcl.title().isEmpty() || tcl.steps().isEmpty()) {
            cerr << "SKIP (parse failed): " << tclPath << "\n";
            ++parseFailed;
            return;
        }
        // isValid() is the app's own import gate: it refuses a profile with an
        // unrecognised step key or a value we cannot read. Without this check
        // the tool that POPULATES the shipped corpus was the one path that
        // bypassed it — saveToFile() would emit a JSON with the offending key
        // simply absent, producing a built-in that looks pristine, imports
        // cleanly, and is missing a setting de1app acts on.
        if (!tcl.isValid()) {
            cerr << "SKIP (invalid): " << tclPath << "\n"
                 << "    " << tcl.validationErrors().join(QLatin1String("\n    ")) << "\n";
            ++parseFailed;
            return;
        }
        const QString outName = Profile::titleToFilename(tcl.title()) + QLatin1String(".json");
        if (fromPlugin && sources.contains(outName)) {
            // Two SOURCES for one output file. The plugin copy still wins — that is
            // the documented rule and it is right today — but it must not win
            // SILENTLY. Before this, the tool reported "plugin overriding base"
            // whether the two agreed or contradicted each other, so a de1app
            // checkout carrying four A-Flow profiles at 6 frames alongside the
            // plugin's 9 read as a clean run. That is de1app issue #350, and our
            // own tooling could not see it: the comparison it ran was
            // plugin-vs-built-in, and the discarded base copy was never examined.
            //
            // Deliberately non-gating. The precedence is a decision, not a guess,
            // so a conflict is not the tool failing to do its job — and #350
            // resolving EITHER way removes the conflict entirely (whichever
            // directory stops shipping the profile leaves one source). What the
            // report has to survive is the case where the surviving source is the
            // stale one, which a human reads off this diff.
            const Source& base = sources[outName];
            // `base` is whatever was ingested first under this output name. Today
            // that is always a base-directory profile, because base files are
            // ingested before plugin files — but two plugin directories shipping the
            // same title would land here too, and then the "base" label below is the
            // wrong word for the discarded side. The paths are printed in full, so
            // the report stays readable either way.
            //
            // Argument order matters for the report: frameDiffReport labels its
            // operands A and B, so pass plugin first to match the order the paths
            // are printed in below. A=plugin, B=base throughout.
            const QString frameDiff = Profile::frameDiffReport(tcl, base.profile);
            // Scalars: raw .tcl text on one side, the parsed profile on the other —
            // the same shape the built-in gate uses, and for the same reason. Routing
            // both sides through the reader would make the check blind to a reader
            // bug affecting both copies equally.
            QString scalarDiff;
            for (const De1AppTcl::ScalarDiff& d :
                 De1AppTcl::compareScalars(content, base.profile.toJsonObject())) {
                scalarDiff += QString("    %1 (tcl %2): A=%3 B=%4\n")
                                  .arg(d.canonical, d.tclKey, d.tclValue, d.jsonValue);
            }
            if (!frameDiff.isEmpty() || !scalarDiff.isEmpty()) {
                cerr << "SOURCE CONFLICT: " << tcl.title() << " (" << outName << ")\n"
                     << "  A = plugin (used):      " << tclPath << "\n"
                     << "  B = base (discarded):   " << base.tclPath << "\n"
                     << scalarDiff << frameDiff
                     << "  → the plugin copy wins. Confirm that is still correct before\n"
                        "    trusting this run; see de1app issue #350.\n";
                ++sourceConflicts;
            }
            overriddenBy.insert(outName, tclPath);
        }
        sources.insert(outName, Source{tclPath, content, outName, tcl, fromPlugin});
    };

    const QStringList baseFiles = src.entryList({QLatin1String("*.tcl")}, QDir::Files, QDir::Name);
    for (const QString& fileName : baseFiles)
        ingest(src.absoluteFilePath(fileName), /*fromPlugin=*/false);

    const QStringList pluginFiles = findPluginTclFiles(src);
    for (const QString& path : pluginFiles)
        ingest(path, /*fromPlugin=*/true);

    if (!overriddenBy.isEmpty()) {
        cout << "Plugin profiles overriding base copies (canonical wins):\n";
        for (auto it = overriddenBy.cbegin(); it != overriddenBy.cend(); ++it)
            cout << "  " << it.key() << "  ←  " << it.value() << "\n";
        if (sourceConflicts)
            cout << "  " << sourceConflicts
                 << " of these DISAGREE with the base copy — see SOURCE CONFLICT on stderr.\n";
        cout << "\n";
    }

    int inSync = 0, different = 0, created = 0, writeRefused = 0;

    // de1app scalars the field map does not cover, aggregated across the corpus.
    // Reported rather than skipped: an incomplete map silently NARROWS the
    // comparison, which is exactly how a 338-row drift was once measured as 4.
    QHash<QString, int> uncovered;

    // Process in a stable order so output is reproducible.
    QStringList outNames = sources.keys();
    std::sort(outNames.begin(), outNames.end());

    for (const QString& outName : outNames) {
        const Source& s = sources[outName];
        const Profile& tcl = s.profile;
        const QString outPath = out.absoluteFilePath(outName);

        for (const QString& key : De1AppTcl::uncoveredTclKeys(s.tclContent))
            uncovered[key] += 1;

        if (QFile::exists(outPath)) {
            Profile existing = Profile::loadFromFile(outPath);
            normaliseSimpleProfile(existing);
            // Mirror the normalisation on the TCL side so we don't trip on
            // simple-profile preinfuseFrameCount (see normaliseSimpleProfile).
            Profile tclNorm = tcl;
            normaliseSimpleProfile(tclNorm);

            // Two independent comparisons. Frames come from the parsed profiles;
            // scalars are read straight out of the .tcl, NOT through
            // loadFromTclString — routing both sides through the reader would
            // make the gate structurally blind to a reader bug, and a reader bug
            // is what put 338 scalar mismatches into the shipped corpus.
            const QString frameDiff = existing.isValid()
                                          ? Profile::frameDiffReport(tclNorm, existing)
                                          : QStringLiteral("  (built-in JSON is invalid)\n");
            const QString scalarDiff = buildScalarDiff(s.tclContent, outPath);

            if (frameDiff.isEmpty() && scalarDiff.isEmpty()) {
                ++inSync;
                if (!(doSync && doForce))
                    continue;
                cout << "FORCE: " << tcl.title() << " (" << outName << ")\n";
                if (syncOverBuiltin(tcl, outPath, cerr))
                    cout << "  → rewritten\n";
                else
                    ++writeRefused;
                continue;
            }

            cout << "DIFF: " << tcl.title() << " (" << outName << ")"
                 << (s.fromPlugin ? " [plugin]" : "") << "\n"
                 << scalarDiff << frameDiff;
            ++different;

            if (doSync) {
                if (syncOverBuiltin(tcl, outPath, cerr))
                    cout << "  → updated\n";
                else
                    ++writeRefused;
            }
        } else {
            cout << "NEW:  " << tcl.title() << " (" << outName << ")"
                 << (s.fromPlugin ? " [plugin]" : "") << "\n";
            ++created;

            if (doSync) {
                if (tcl.saveToFile(outPath)) {
                    cout << "  → created\n";
                } else {
                    cerr << "  → ERROR: failed to write " << outPath << "\n";
                    ++writeRefused;
                }
            }
        }
    }

    if (!uncovered.isEmpty()) {
        QStringList keys = uncovered.keys();
        std::sort(keys.begin(), keys.end());
        cout << "\nUNCOMPARED de1app keys (present in the corpus, absent from the field map):\n";
        for (const QString& k : keys)
            cout << "  " << k << "  (" << uncovered.value(k) << " profiles)\n";
        cout << "  → add to De1AppTcl::scalarFields() to compare, or to\n"
             << "    De1AppTcl::nonScalarTclKeys() with a reason not to.\n";
    }

    cout << "\n";
    if (doSync) {
        cout << "Sync complete: " << different << " updated, " << created << " created, "
             << inSync << " already in sync, " << parseFailed << " skipped\n";
    } else {
        cout << "Compare complete: " << different << " different, " << created << " missing, "
             << inSync << " in sync, " << parseFailed << " skipped\n";
        if (different > 0 || created > 0)
            cout << "Run with --sync to update built-in profiles.\n";
    }
    if (parseFailed)   cerr << "  " << parseFailed   << " source profile(s) unreadable, unparseable or invalid\n";
    if (writeRefused)  cerr << "  " << writeRefused  << " write(s) refused or failed — files left untouched\n";
    if (sourceConflicts) cerr << "  " << sourceConflicts
                              << " plugin/base source conflict(s) — plugin copy used, verify that is right\n";

    // A skipped source and an uncompared key both NARROW the comparison, and a
    // narrowed comparison that reports success is how this drift was measured
    // as 4 rows when it was 414. Exiting 0 regardless meant a run against a
    // half-unreadable checkout — wrong path, bad permissions, an uninitialised
    // submodule — compared a fraction of the corpus and still read as a pass.
    //
    // Drift itself does NOT gate: reporting differences is what compare mode is
    // FOR, and tst_tclimport is the gate for those. What gates is the tool being
    // unable to do the job it claims to have done.
    return (parseFailed || writeRefused || !uncovered.isEmpty()) ? 1 : 0;
}
