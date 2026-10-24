#pragma once

#include <QVariantMap>
#include <QStringList>
#include <cmath>

// Shared preference vocabulary for native settings and the paired browser projection.
#define DECENZA_GRAPH_TRACES(X) \
    X(showPortalEc, setShowPortalEc, kKeyShowPortalEc, true, showPortalEcChanged) \
    X(showPortalTemperature, setShowPortalTemperature, kKeyShowPortalTemperature, true, showPortalTemperatureChanged) \
    X(showPressure, setShowPressure, kKeyShowPressure, true, showPressureChanged) \
    X(showFlow, setShowFlow, kKeyShowFlow, true, showFlowChanged) \
    X(showTemperature, setShowTemperature, kKeyShowTemperature, true, showTemperatureChanged) \
    X(showWeight, setShowWeight, kKeyShowWeight, true, showWeightChanged) \
    X(showWeightFlow, setShowWeightFlow, kKeyShowWeightFlow, true, showWeightFlowChanged) \
    X(showResistance, setShowResistance, kKeyShowResistance, false, showResistanceChanged) \
    X(showConductance, setShowConductance, kKeyShowConductance, false, showConductanceChanged) \
    X(showConductanceDerivative, setShowConductanceDerivative, kKeyShowConductanceDerivative, false, showConductanceDerivativeChanged) \
    X(showDarcyResistance, setShowDarcyResistance, kKeyShowDarcyResistance, false, showDarcyResistanceChanged) \
    X(showTemperatureMix, setShowTemperatureMix, kKeyShowTemperatureMix, false, showTemperatureMixChanged) \
    X(showTemperatureMixGoal, setShowTemperatureMixGoal, kKeyShowTemperatureMixGoal, false, showTemperatureMixGoalChanged)

// Steam traces are a separate graph surface. Keep these outside DECENZA_GRAPH_TRACES:
// shot graph legends enumerate that macro and must never expose Steam-only toggles.
#define DECENZA_STEAM_GRAPH_TRACES(X) \
    X(showSteamPressure, setShowSteamPressure, kKeyShowSteamPressure, true, showSteamPressureChanged) \
    X(showSteamFlow, setShowSteamFlow, kKeyShowSteamFlow, true, showSteamFlowChanged) \
    X(showSteamTemperature, setShowSteamTemperature, kKeyShowSteamTemperature, true, showSteamTemperatureChanged)

namespace GraphPreferences {
inline QVariantMap defaults()
{
    QVariantMap values;
#define ADD_DEFAULT(Getter, Setter, Key, Default, Signal) values.insert(QStringLiteral(#Getter), Default);
    DECENZA_GRAPH_TRACES(ADD_DEFAULT)
    DECENZA_STEAM_GRAPH_TRACES(ADD_DEFAULT)
#undef ADD_DEFAULT
    values.insert(QStringLiteral("advancedMode"), false);
    values.insert(QStringLiteral("flowMultiplier"), 2);
    values.insert(QStringLiteral("rightAxisMode"), QStringLiteral("weight"));
    return values;
}

inline QVariantMap shotDefaults()
{
    QVariantMap values;
#define ADD_DEFAULT(Getter, Setter, Key, Default, Signal) values.insert(QStringLiteral(#Getter), Default);
    DECENZA_GRAPH_TRACES(ADD_DEFAULT)
#undef ADD_DEFAULT
    values.insert(QStringLiteral("advancedMode"), false);
    values.insert(QStringLiteral("flowMultiplier"), 2);
    values.insert(QStringLiteral("rightAxisMode"), QStringLiteral("weight"));
    return values;
}

inline QVariantMap steamDefaults()
{
    QVariantMap values;
#define ADD_DEFAULT(Getter, Setter, Key, Default, Signal) values.insert(QStringLiteral(#Getter), Default);
    DECENZA_STEAM_GRAPH_TRACES(ADD_DEFAULT)
#undef ADD_DEFAULT
    return values;
}

inline bool validate(const QVariantMap& values)
{
    const auto expected = defaults();
    if (values.size() != expected.size()) return false;
    for (auto it = expected.cbegin(); it != expected.cend(); ++it) {
        const auto found = values.constFind(it.key());
        if (found == values.cend()) return false;
        if (it->metaType().id() == QMetaType::Bool && found->metaType().id() != QMetaType::Bool)
            return false;
    }
    const auto multiplier = values.value(QStringLiteral("flowMultiplier"));
    const int type = multiplier.metaType().id();
    if (type != QMetaType::Int && type != QMetaType::UInt
        && type != QMetaType::LongLong && type != QMetaType::ULongLong
        && type != QMetaType::Double)
        return false;
    const double number = multiplier.toDouble();
    if (!std::isfinite(number) || number != std::floor(number) || number < 1 || number > 3)
        return false;
    const auto mode = values.value(QStringLiteral("rightAxisMode"));
    return mode.metaType().id() == QMetaType::QString
        && QStringList{QStringLiteral("weight"), QStringLiteral("temperature"), QStringLiteral("flow")}.contains(mode.toString());
}

inline bool validateSteam(const QVariantMap& values)
{
    const auto expected = steamDefaults();
    if (values.size() != expected.size()) return false;
    for (auto it = expected.cbegin(); it != expected.cend(); ++it) {
        const auto found = values.constFind(it.key());
        if (found == values.cend() || found->metaType().id() != QMetaType::Bool)
            return false;
    }
    return true;
}
}
