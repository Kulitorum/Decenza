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
// directories under `de1plus/plugins/*/profiles/` are scanned automatically when the
// parent of the first argument is named "de1plus"; if a plugin ships a profile with the
// same identity as a base profile, the plugin copy wins (canonical source).

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QRegularExpression>
#include <QDebug>
#include <QHash>

#include "profile/profile.h"
#include "profile/profileframe.h"

// titleToFilename: matches ProfileManager::titleToFilename.
// NFD decomposition handles accented chars (é→e, ñ→n, etc.)
static QString titleToFilename(const QString& title)
{
    QString fn = title.normalized(QString::NormalizationForm_D);
    fn.remove(QRegularExpression(QStringLiteral("[\\x{0300}-\\x{036f}]")));
    QString sanitized;
    for (const QChar& c : fn) {
        if (c.isLetterOrNumber())
            sanitized += c.toLower();
        else
            sanitized += QLatin1Char('_');
    }
    while (sanitized.contains(QLatin1String("__")))
        sanitized.replace(QLatin1String("__"), QLatin1String("_"));
    while (sanitized.startsWith(QLatin1Char('_'))) sanitized.remove(0, 1);
    while (sanitized.endsWith(QLatin1Char('_')))   sanitized.chop(1);
    if (sanitized.length() > 50) sanitized = sanitized.left(50);
    return sanitized;
}

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

// Build a human-readable diff between a TCL-parsed profile and its built-in JSON.
// Mirrors the logic of Profile::functionallyEqual() so the report reflects exactly
// what would cause the import check to flag the profile as "different".
static QString buildDiff(const Profile& tcl, const Profile& builtin)
{
    QString report;

    // Header-level mismatches always print, even when one side has 0 frames —
    // otherwise simple-profile diffs render with an empty body.
    if (tcl.steps().size() != builtin.steps().size())
        report += QString("  step count: TCL=%1 JSON=%2\n")
                      .arg(tcl.steps().size()).arg(builtin.steps().size());
    if (tcl.preinfuseFrameCount() != builtin.preinfuseFrameCount())
        report += QString("  preinfuseFrameCount: TCL=%1 JSON=%2\n")
                      .arg(tcl.preinfuseFrameCount()).arg(builtin.preinfuseFrameCount());

    const qsizetype n = qMin(tcl.steps().size(), builtin.steps().size());
    for (qsizetype i = 0; i < n; ++i) {
        const ProfileFrame& a = tcl.steps()[i];
        const ProfileFrame& b = builtin.steps()[i];
        const QString p = QString("  FRAME[%1] ").arg(i);

        if (a.pump       != b.pump)       report += p + "pump: TCL=" + a.pump + " JSON=" + b.pump + "\n";
        if (a.sensor     != b.sensor)     report += p + "sensor: TCL=" + a.sensor + " JSON=" + b.sensor + "\n";
        if (a.transition != b.transition) report += p + "transition: TCL=" + a.transition + " JSON=" + b.transition + "\n";
        if (a.popup      != b.popup)      report += p + "popup: TCL='" + a.popup + "' JSON='" + b.popup + "'\n";
        if (a.exitIf     != b.exitIf)     report += p + "exitIf: TCL=" + QString::number(a.exitIf) + " JSON=" + QString::number(b.exitIf) + "\n";
        if (a.exitIf && a.exitType != b.exitType)
            report += p + "exitType: TCL=" + a.exitType + " JSON=" + b.exitType + "\n";

        auto chkF = [&](const QString& lbl, double va, double vb) {
            if (qAbs(va - vb) > 0.1)
                report += p + lbl + ": TCL=" + QString::number(va) + " JSON=" + QString::number(vb) + "\n";
        };
        chkF("temperature", a.temperature, b.temperature);
        if (a.pump == "pressure") {
            chkF("pressure", a.pressure, b.pressure);
            if (a.flow > 0.1 && b.flow > 0.1) chkF("flow", a.flow, b.flow);
        } else {
            chkF("flow", a.flow, b.flow);
            if (a.pressure > 0.1 && b.pressure > 0.1) chkF("pressure", a.pressure, b.pressure);
        }
        chkF("seconds",     a.seconds,     b.seconds);
        chkF("volume",      a.volume,      b.volume);

        // Only compare the active exit threshold (inactive ones are noise from de1app TCL)
        if (a.exitIf) {
            if      (a.exitType == "pressure_over")  chkF("exitPressureOver",  a.exitPressureOver,  b.exitPressureOver);
            else if (a.exitType == "pressure_under") chkF("exitPressureUnder", a.exitPressureUnder, b.exitPressureUnder);
            else if (a.exitType == "flow_over")      chkF("exitFlowOver",      a.exitFlowOver,      b.exitFlowOver);
            else if (a.exitType == "flow_under")     chkF("exitFlowUnder",     a.exitFlowUnder,     b.exitFlowUnder);
        }

        chkF("exitWeight",             a.exitWeight,             b.exitWeight);
        chkF("maxFlowOrPressure",      a.maxFlowOrPressure,      b.maxFlowOrPressure);
        chkF("maxFlowOrPressureRange", a.maxFlowOrPressureRange, b.maxFlowOrPressureRange);
    }

    return report;
}

// Walk `<de1plus>/plugins/*/profiles/*.tcl` if `baseDir` looks like `<de1plus>/profiles`.
// Returns absolute file paths to every plugin TCL profile; empty list otherwise.
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
        QTextStream(stderr) << "Usage: profile_sync <de1app_profiles_dir> <builtin_profiles_dir> [--sync]\n"
                            << "\n"
                            << "  Without --sync: report differences only (compare mode)\n"
                            << "  With    --sync: also overwrite stale JSONs and create missing ones\n"
                            << "\n"
                            << "  Plugin profiles under <de1app_profiles_dir>/../plugins/*/profiles/\n"
                            << "  are scanned automatically and override base profiles with the same\n"
                            << "  output filename (canonical source wins).\n";
        return 1;
    }

    const QString de1appDir  = args[1];
    const QString builtinDir = args[2];
    const bool    doSync     = args.contains(QLatin1String("--sync"));

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

    // Build the unified TCL source list. Base files come first, then plugin files
    // override base entries when the resulting filename collides — that way a
    // canonical 9-frame plugin A-Flow profile beats the stale 6-frame base copy.
    struct Source {
        QString tclPath;       // absolute path to the TCL source
        QString outFilename;   // titleToFilename(profile.title()) + ".json"
        Profile profile;       // already-parsed (avoid re-parsing later)
        bool fromPlugin = false;
    };
    QHash<QString, Source> sources;          // key: outFilename
    QHash<QString, QString> overriddenBy;    // outFilename -> plugin path (for reporting)
    int parseFailed = 0;

    auto ingest = [&](const QString& tclPath, bool fromPlugin) {
        QFile f(tclPath);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            cout << "SKIP (cannot read): " << tclPath << "\n";
            ++parseFailed;
            return;
        }
        const Profile tcl = Profile::loadFromTclString(QTextStream(&f).readAll());
        if (tcl.title().isEmpty() || tcl.steps().isEmpty()) {
            cout << "SKIP (parse failed): " << tclPath << "\n";
            ++parseFailed;
            return;
        }
        const QString outName = titleToFilename(tcl.title()) + QLatin1String(".json");
        if (fromPlugin && sources.contains(outName))
            overriddenBy.insert(outName, tclPath);
        sources.insert(outName, Source{tclPath, outName, tcl, fromPlugin});
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
        cout << "\n";
    }

    int inSync = 0, different = 0, created = 0;

    // Process in a stable order so output is reproducible.
    QStringList outNames = sources.keys();
    std::sort(outNames.begin(), outNames.end());

    for (const QString& outName : outNames) {
        const Source& s = sources[outName];
        const Profile& tcl = s.profile;
        const QString outPath = out.absoluteFilePath(outName);

        if (QFile::exists(outPath)) {
            Profile existing = Profile::loadFromFile(outPath);
            normaliseSimpleProfile(existing);
            // Mirror the normalisation on the TCL side so we don't trip on
            // simple-profile preinfuseFrameCount (see normaliseSimpleProfile).
            Profile tclNorm = tcl;
            normaliseSimpleProfile(tclNorm);

            if (existing.isValid() && Profile::functionallyEqual(tclNorm, existing)) {
                ++inSync;
                continue;
            }

            const QString diff = buildDiff(tclNorm, existing);
            cout << "DIFF: " << tcl.title() << " (" << outName << ")"
                 << (s.fromPlugin ? " [plugin]" : "") << "\n" << diff;
            ++different;

            if (doSync) {
                if (tcl.saveToFile(outPath))
                    cout << "  → updated\n";
                else
                    cout << "  → ERROR: failed to write\n";
            }
        } else {
            cout << "NEW:  " << tcl.title() << " (" << outName << ")"
                 << (s.fromPlugin ? " [plugin]" : "") << "\n";
            ++created;

            if (doSync) {
                if (tcl.saveToFile(outPath))
                    cout << "  → created\n";
                else
                    cout << "  → ERROR: failed to write\n";
            }
        }
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

    return 0;
}
