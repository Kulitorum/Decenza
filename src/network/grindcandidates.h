#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include "../core/settings_dye.h"

// Grind/RPM candidate generation for the web <datalist> helper
// (replace-grind-inputs-with-picker), split out of
// ShotServer::handleGrindCandidatesApi so it can be tested.
//
// The split exists for the reason tst_exifdate documents one file over: logic
// living inside ShotServer cannot be tested without linking a web server, a
// storage backend, a DE1 device and an AI manager — and the last time that
// asymmetry was tolerated here, a rename silently turned a whole path into
// dead code and nothing noticed. This function is the C++ TWIN of
// GrindRowSource.qml's stepping, including the click-indexed negative re-check
// that the QML side needed a bug fix to get right; an untested hand-duplicate
// of a just-fixed bug is exactly the thing that rots. Everything the caller
// must fetch from storage (the two history-derived steps, the observed
// settings) arrives as a plain value, so tests need no database.
//
// Deliberate divergence from the app's wheels: the window here is ±5 steps,
// not the wheel's effectively-unbounded ±400. A <datalist> is a SUGGESTION
// list — a dropdown of 801 rows would be unusable, and free text still reaches
// any value, which is the whole reason the web keeps a text input instead of a
// ported wheel (design D6). The wheel needs a wide window because spinning is
// its only travel; the datalist does not.
namespace GrindCandidates {

// Candidate window half-width, in steps (11 rows total). See the divergence
// note above before widening this to match the app.
inline constexpr int kWindowSteps = 5;
// Cap on the observed-history fallback list.
inline constexpr int kHistoryCap = 11;
// Neutral RPM anchor when the record has none, matching
// GrindRowSource.rpmDefaultAnchor.
inline constexpr qint64 kRpmDefaultAnchor = 1000;

// What the caller resolves from storage before generating. Keeping these as
// values (rather than a storage pointer) is what makes the generator testable.
struct Inputs {
    QString brand;
    QString model;
    QString current;             // the record's current grind, verbatim
    qint64 rpm = 0;              // the record's current RPM; <= 0 = unset
    double grindStep = 0.0;      // history-derived; <= 0 falls back to 1.0
    double rpmStep = 0.0;        // history-derived; <= 0 falls back to 50
    QStringList observed;        // observed settings for this grinder (empty
                                 // model = cross-grinder history, resolved by
                                 // the caller)
};

// Decimal places come from the STEP, not the value (1.0 -> 0, 0.5 -> 1,
// 0.25 -> 2), mirroring GrindRowSource._stepDecimals. Bounded via the 3-decimal
// round-trip so a float-dirty step (0.1 + 0.2) cannot produce a 17-decimal
// label.
inline int stepDecimals(double step)
{
    QString s = QString::number(step, 'f', 3);
    while (s.endsWith(QLatin1Char('0'))) s.chop(1);
    if (s.endsWith(QLatin1Char('.'))) s.chop(1);
    const qsizetype dot = s.indexOf(QLatin1Char('.'));
    return dot < 0 ? 0 : static_cast<int>(s.size() - dot - 1);
}

// Format a stepped value at the step's precision, trailing zeros stripped so
// labels match the display convention ("7.50" -> "7.5", "7.00" -> "7").
inline QString formatStepped(double v, int decimals)
{
    QString s = QString::number(v, 'f', decimals);
    if (s.contains(QLatin1Char('.'))) {
        while (s.endsWith(QLatin1Char('0'))) s.chop(1);
        if (s.endsWith(QLatin1Char('.'))) s.chop(1);
    }
    return s;
}

// Build the {grind: [...], rpm: [...]} payload.
//
// grind: catalog stepping first (notation-aware; click-indexed grinders skip
// below-floor rows), then a plain-numeric fallback carrying the SAME
// click-indexed re-check — the catalog returns "" for those rows and they fall
// through to the fallback, which would otherwise resurrect the refused
// negative for a compound grinder whose value is logged as a plain number
// ("2.5" on a Mignon). No negative skip otherwise: a stepless collar's zero is
// a user-set calibration reference and finer-than-zero is a real dial
// position. Too few distinct candidates (no current value, letters, an
// unparseable notation) falls back to observed history.
//
// rpm: empty unless the grinder is RPM-capable — an empty list IS the
// capability verdict the web helper hides the RPM field on, so the gate has
// one source of truth shared with the app.
inline QJsonObject build(SettingsDye* dye, const Inputs& in)
{
    QJsonObject out{{QStringLiteral("grind"), QJsonArray()},
                    {QStringLiteral("rpm"), QJsonArray()}};
    if (!dye)
        return out;

    const double step = in.grindStep > 0.0 ? in.grindStep : 1.0;
    const int decimals = stepDecimals(step);

    QJsonArray grind;
    if (!in.current.isEmpty()) {
        const bool clickIndexed = dye->grinderIsClickIndexed(in.brand, in.model);
        QSet<QString> seen;
        for (int n = -kWindowSteps; n <= kWindowSteps; ++n) {
            QString v = dye->stepGrinderSetting(in.brand, in.model, in.current,
                                                n * step, decimals);
            if (v.isEmpty()) {
                bool numeric = false;
                const double num = in.current.toDouble(&numeric);
                if (numeric) {
                    const double stepped = num + n * step;
                    if (!(stepped < 0.0 && clickIndexed))
                        v = formatStepped(stepped, decimals);
                }
            }
            if (v.isEmpty() || seen.contains(v))
                continue;
            seen.insert(v);
            grind.append(v);
        }
    }
    // Observed-history fallback, mirroring the app's wheel fallback.
    if (grind.size() <= 2) {
        grind = QJsonArray();
        if (!in.current.isEmpty() && !in.observed.contains(in.current))
            grind.append(in.current);
        for (const QString& v : in.observed) {
            if (grind.size() >= kHistoryCap)
                break;
            grind.append(v);
        }
    }
    out[QStringLiteral("grind")] = grind;

    QJsonArray rpmArr;
    if (dye->grinderRpmCapable(in.brand, in.model)) {
        const qint64 rpmStep = in.rpmStep > 0.0 ? qRound64(in.rpmStep) : 50;
        const qint64 base = in.rpm > 0 ? in.rpm : kRpmDefaultAnchor;
        for (int n = -kWindowSteps; n <= kWindowSteps; ++n) {
            const qint64 r = base + n * rpmStep;
            if (r <= 0)   // a motor has no negative speed, and 0 is the unset sentinel
                continue;
            if (!rpmArr.contains(QJsonValue(static_cast<double>(r))))
                rpmArr.append(static_cast<double>(r));
        }
    }
    out[QStringLiteral("rpm")] = rpmArr;
    return out;
}

} // namespace GrindCandidates
