// profile_sync.cpp — Developer tool to compare and sync built-in profile JSONs
// against de1app TCL source profiles using the same C++ parser as the app and tests.
//
// Replaces scripts/compare_profiles.py and scripts/sync_profiles.py.
//
// Usage:
//   profile_sync <de1app_profiles_dir> <builtin_profiles_dir> [--sync]
//
//   Without --sync: report all differences (compare mode)
//   With    --sync: also overwrite stale JSONs and create missing ones (sync mode)

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QDebug>

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

// Build a human-readable diff between a TCL-parsed profile and its built-in JSON.
// Mirrors the logic of Profile::functionallyEqual() so the report reflects exactly
// what would cause the import check to flag the profile as "different".
static QString buildDiff(const Profile& tcl, const Profile& builtin)
{
    QString report;

    if (tcl.steps().size() != builtin.steps().size())
        report += QString("  step count: TCL=%1 JSON=%2\n")
                      .arg(tcl.steps().size()).arg(builtin.steps().size());

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

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    const QStringList args = app.arguments();
    if (args.size() < 3) {
        QTextStream(stderr) << "Usage: profile_sync <de1app_profiles_dir> <builtin_profiles_dir> [--sync]\n"
                            << "\n"
                            << "  Without --sync: report differences only (compare mode)\n"
                            << "  With    --sync: also overwrite stale JSONs and create missing ones\n";
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

    int inSync = 0, different = 0, created = 0, failed = 0;

    QTextStream cout(stdout);

    const QStringList tclFiles = src.entryList({QLatin1String("*.tcl")}, QDir::Files, QDir::Name);
    for (const QString& fileName : tclFiles) {
        const QString tclPath = src.absoluteFilePath(fileName);

        QFile f(tclPath);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            cout << "SKIP (cannot read): " << fileName << "\n";
            ++failed;
            continue;
        }
        const Profile tcl = Profile::loadFromTclString(QTextStream(&f).readAll());
        if (tcl.title().isEmpty() || tcl.steps().isEmpty()) {
            cout << "SKIP (parse failed): " << fileName << "\n";
            ++failed;
            continue;
        }

        const QString fn      = titleToFilename(tcl.title());
        const QString outPath = out.absoluteFilePath(fn + QLatin1String(".json"));

        if (QFile::exists(outPath)) {
            const Profile existing = Profile::loadFromFile(outPath);
            if (existing.isValid() && Profile::functionallyEqual(tcl, existing)) {
                ++inSync;
                continue;
            }

            const QString diff = buildDiff(tcl, existing);
            cout << "DIFF: " << tcl.title() << " (" << fn << ".json)\n" << diff;
            ++different;

            if (doSync) {
                if (tcl.saveToFile(outPath))
                    cout << "  → updated\n";
                else
                    cout << "  → ERROR: failed to write\n";
            }
        } else {
            cout << "NEW:  " << tcl.title() << " (" << fn << ".json)\n";
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
             << inSync << " already in sync, " << failed << " skipped\n";
    } else {
        cout << "Compare complete: " << different << " different, " << created << " missing, "
             << inSync << " in sync, " << failed << " skipped\n";
        if (different > 0 || created > 0)
            cout << "Run with --sync to update built-in profiles.\n";
    }

    return 0;
}
