#include "settings_graph.h"

namespace {
// Storage keys keep the `graph/` prefix they had before this class existed. Renaming one
// would silently discard every user's saved choice for that trace.
constexpr QLatin1String kKeyShowPressure{"graph/showPressure"};
constexpr QLatin1String kKeyShowFlow{"graph/showFlow"};
constexpr QLatin1String kKeyShowTemperature{"graph/showTemperature"};
constexpr QLatin1String kKeyShowWeight{"graph/showWeight"};
constexpr QLatin1String kKeyShowWeightFlow{"graph/showWeightFlow"};
constexpr QLatin1String kKeyShowResistance{"graph/showResistance"};
constexpr QLatin1String kKeyShowConductance{"graph/showConductance"};
constexpr QLatin1String kKeyShowConductanceDerivative{"graph/showConductanceDerivative"};
constexpr QLatin1String kKeyShowDarcyResistance{"graph/showDarcyResistance"};
constexpr QLatin1String kKeyShowTemperatureMix{"graph/showTemperatureMix"};
constexpr QLatin1String kKeyShowTemperatureMixGoal{"graph/showTemperatureMixGoal"};
constexpr QLatin1String kKeyAdvancedMode{"shotReview/advancedMode"};
constexpr QLatin1String kKeyFlowMultiplier{"graph/flowMultiplier"};
constexpr QLatin1String kKeyRightAxisMode{"graph/rightAxisMode"};

// Superseded by rightAxisMode. Still read, never written — see resolveRightAxisMode().
constexpr QLatin1String kKeyLegacyShowWeightAxis{"graph/showWeightAxis"};
}  // namespace

SettingsGraph::SettingsGraph(QObject* parent) : QObject(parent) {}

bool SettingsGraph::readBool(QLatin1String key, bool defaultValue) const {
    const QVariant v = m_settings.value(key);
    if (!v.isValid()) return defaultValue;
    // Same coercion as Settings::boolValue(): the INI backend used on Android/Linux/iOS
    // round-trips booleans as the strings "true"/"false", and JavaScript reads "false" as
    // truthy. Doing it here keeps QML from having to.
    return v.toBool();
}

void SettingsGraph::writeBool(QLatin1String key, bool value, void (SettingsGraph::*signal)()) {
    if (readBool(key, !value) == value) return;
    m_settings.setValue(key, value);
    emit(this->*signal)();
}

#define DECENZA_GRAPH_BOOL(Getter, Setter, Key, Default, Signal)      \
    bool SettingsGraph::Getter() const {                             \
        return readBool(Key, Default);                               \
    }                                                                \
    void SettingsGraph::Setter(bool show) {                          \
        writeBool(Key, show, &SettingsGraph::Signal);                \
    }

// Eleven properties differing only in key, default and signal. Written out by hand this is
// 88 lines in which a single mismatched key or default hides indefinitely — the defaults in
// particular were already duplicated across five QML files before this class existed.
DECENZA_GRAPH_BOOL(showPressure, setShowPressure, kKeyShowPressure, true, showPressureChanged)
DECENZA_GRAPH_BOOL(showFlow, setShowFlow, kKeyShowFlow, true, showFlowChanged)
DECENZA_GRAPH_BOOL(showTemperature, setShowTemperature, kKeyShowTemperature, true, showTemperatureChanged)
DECENZA_GRAPH_BOOL(showWeight, setShowWeight, kKeyShowWeight, true, showWeightChanged)
DECENZA_GRAPH_BOOL(showWeightFlow, setShowWeightFlow, kKeyShowWeightFlow, true, showWeightFlowChanged)
DECENZA_GRAPH_BOOL(showResistance, setShowResistance, kKeyShowResistance, false, showResistanceChanged)
DECENZA_GRAPH_BOOL(showConductance, setShowConductance, kKeyShowConductance, false, showConductanceChanged)
DECENZA_GRAPH_BOOL(showConductanceDerivative, setShowConductanceDerivative, kKeyShowConductanceDerivative, false, showConductanceDerivativeChanged)
DECENZA_GRAPH_BOOL(showDarcyResistance, setShowDarcyResistance, kKeyShowDarcyResistance, false, showDarcyResistanceChanged)
DECENZA_GRAPH_BOOL(showTemperatureMix, setShowTemperatureMix, kKeyShowTemperatureMix, false, showTemperatureMixChanged)
DECENZA_GRAPH_BOOL(showTemperatureMixGoal, setShowTemperatureMixGoal, kKeyShowTemperatureMixGoal, false, showTemperatureMixGoalChanged)

bool SettingsGraph::advancedMode() const {
    return readBool(kKeyAdvancedMode, false);
}

void SettingsGraph::setAdvancedMode(bool enabled) {
    writeBool(kKeyAdvancedMode, enabled, &SettingsGraph::advancedModeChanged);
}

#undef DECENZA_GRAPH_BOOL

int SettingsGraph::resolveFlowMultiplier(const QVariant& stored) {
    // Go through toInt()'s ok flag rather than trusting the QVariant's type: the INI backend
    // hands numbers back as strings, and a bare toInt() turns anything unparseable into 0.
    bool ok = false;
    const int value = stored.toInt(&ok);
    if (!ok) return kFlowMultiplierDefault;
    if (value < kFlowMultiplierMin || value > kFlowMultiplierMax) return kFlowMultiplierDefault;
    return value;
}

int SettingsGraph::flowMultiplier() const {
    return resolveFlowMultiplier(m_settings.value(kKeyFlowMultiplier));
}

void SettingsGraph::setFlowMultiplier(int multiplier) {
    const int resolved = resolveFlowMultiplier(multiplier);
    if (flowMultiplier() == resolved) return;
    m_settings.setValue(kKeyFlowMultiplier, resolved);
    emit flowMultiplierChanged();
}

QString SettingsGraph::resolveRightAxisMode(const QVariant& storedMode,
                                            const QVariant& legacyShowWeightAxis) {
    const QString mode = storedMode.toString();
    if (mode == kRightAxisWeight || mode == kRightAxisTemperature || mode == kRightAxisFlow) {
        return mode;
    }
    // No usable mode. Fall back to whatever the superseded boolean said, so an existing
    // user's choice survives the upgrade without anything being written.
    if (legacyShowWeightAxis.isValid()) {
        return legacyShowWeightAxis.toBool() ? kRightAxisWeight : kRightAxisTemperature;
    }
    return kRightAxisWeight;
}

QString SettingsGraph::rightAxisMode() const {
    return resolveRightAxisMode(m_settings.value(kKeyRightAxisMode),
                                m_settings.value(kKeyLegacyShowWeightAxis));
}

void SettingsGraph::setRightAxisMode(const QString& mode) {
    // Resolve rather than store blind: a caller passing an unrecognised mode would otherwise
    // persist a value that every subsequent read silently rewrites to weight.
    const QString resolved = resolveRightAxisMode(mode, QVariant());
    if (rightAxisMode() == resolved) return;
    m_settings.setValue(kKeyRightAxisMode, resolved);
    emit rightAxisModeChanged();
}

QString SettingsGraph::nextRightAxisMode(const QString& current) {
    if (current == kRightAxisWeight) return kRightAxisTemperature;
    if (current == kRightAxisTemperature) return kRightAxisFlow;
    return kRightAxisWeight;
}

QString SettingsGraph::cycleRightAxisMode() {
    // Advance from the RESOLVED mode, not the raw stored one. On the first tap after an
    // upgrade the new key is still absent, and stepping from that empty value would discard
    // the user's superseded weight/temperature choice instead of continuing from it.
    const QString next = nextRightAxisMode(rightAxisMode());
    setRightAxisMode(next);
    return next;
}
