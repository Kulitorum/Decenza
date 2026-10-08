#pragma once

#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include <optional>

class ShotProjection;

// What changed between shots and what the shots did differently, relative to a
// base shot. The one definition behind every surface that compares shots: the
// Compare page, the web /compare/ page, MCP shots_compare and the advisor's
// change detection. Values are raw numbers plus unit tokens; display text is
// built by each surface, where the translations and the temperature unit live.
namespace ShotComparison {

// Two inputs within these are the same input. Set for the advisor's adherence
// scoring: a grinder click rounds to a quarter step, a scale reads ±0.3 g, and a
// ratio-derived yield target rounds to ±0.5 g.
inline constexpr double kGrinderStepTolerance = 0.25;
inline constexpr double kDoseToleranceG = 0.3;
inline constexpr double kYieldToleranceG = 0.5;
inline constexpr int kRpmTolerance = 25;
inline constexpr double kTemperatureToleranceC = 0.05;

// Cup weight that counts as the first drop.
inline constexpr double kFirstDropG = 0.5;
// Resistance is averaged only where the puck is actually flowing.
inline constexpr double kResistanceMinFlowMlPerSec = 0.5;

// Notation-insensitive: "1 + 4" is "1+4", "23.5 1400rpm" is "23.5". An empty or
// unparseable side compares as the same — absence is not evidence of a regrind.
// The advisor's adherence check allows a quarter step of click rounding; showing
// what the user changed passes 0, because a recorded setting that differs at all
// was set on purpose (a stepless grinder moves in tenths).
bool sameGrinderSetting(const QString& a, const QString& b,
                        double tolerance = kGrinderStepTolerance);

// The stop-at-weight target: the stored value, else the saved profile's.
double effectiveTargetWeightG(const ShotProjection& shot);

enum class FieldState { Same, Changed, OneSided };

struct InputField {
    QString key;
    QString unit;              // "" for text; "g", "rpm", "celsius"
    FieldState state = FieldState::Same;
    QString baseText;
    QString shotText;
    bool numeric = false;
    double baseValue = 0.0;
    double shotValue = 0.0;
    std::optional<double> delta;   // only when both sides are numbers on one scale
    // Dialled between shots (profile, temperature, dose, target, grind, RPM), as opposed
    // to the setup and bean.
    bool dialIn = false;
};

struct InputDiff {
    QList<InputField> fields;  // every input, display order, grouped
    const InputField* field(const QString& key) const;
    // Changed only. A value recorded on one shot is OneSided, not a change: the
    // advisor relies on that to keep an unrecorded RPM from reading as a decision.
    bool changed(const QString& key) const;
    bool anyDifference() const;
};

InputDiff diffInputs(const ShotProjection& base, const ShotProjection& shot);

struct MetricDef {
    const char* key;
    const char* unit;   // "s", "g", "", "bar", "mlPerSec", "gPerSec", "celsius",
                        // "celsiusDelta", "percent"
    int decimals;       // display precision; a Δ that rounds to zero here is no Δ
    bool more;          // behind "Show more"
    // The smallest change worth naming in a summary. Ranking by relative change
    // put a 0.1 °C temperature sag in the top three, because small numbers move a
    // lot proportionally; ranking by multiples of this does not. 0: never ranked.
    double notable;
};
const QList<MetricDef>& metricDefs();

// Missing keys are metrics the shot cannot support (no weight curve, no pour marker…).
QMap<QString, double> metricsFor(const ShotProjection& shot);

// The badge keys raised on a shot, in display order.
QStringList badgesFor(const ShotProjection& shot);

// What moved between two shots, for a reader that sees them as a pair: the
// advisor's changeFromPrev/changeFromBest and each MCP comparison. Inputs that
// differ (a side recorded on one shot only is null) and outcomes whose Δ is not
// zero at display precision, each as {from, to, delta}. Empty when nothing moved.
// DialIn leaves out the setup and bean, for a caller that already states them per shot
// (the advisor's identity overrides) and would otherwise say each change twice.
enum class PairInputs { All, DialIn };
QJsonObject pairChanges(const ShotProjection& from, const ShotProjection& to,
                        PairInputs inputs = PairInputs::All);
// The same, with each shot's metricsFor() already computed, for a caller walking a chain
// of shots so each shot's curves are read once.
QJsonObject pairChanges(const ShotProjection& from, const ShotProjection& to,
                        const QMap<QString, double>& fromMetrics,
                        const QMap<QString, double>& toMetrics,
                        PairInputs inputs = PairInputs::All);

// Which metric rows compare() emits: every metric at least one shot supports, or
// also the default rows (those not behind "Show more") with empty cells, for a
// page that always lists duration, yield and ratio and shows "—" for a shot
// without weight data.
enum class MetricRows { Supported, Defaults };

// The whole comparison, base first and the other shots in the given order.
// `baseIndex` indexes `shots`.
QJsonObject compare(const QList<ShotProjection>& shots, qsizetype baseIndex);
QJsonObject compare(const QList<ShotProjection>& shots, qsizetype baseIndex, MetricRows rows);

} // namespace ShotComparison
