#pragma once

#include <QObject>
#include <QString>
#include <QVariant>
#include "appsettings.h"
#include "graphpreferences.h"

// Display preferences for the shot graphs: which traces are drawn, how flow is scaled,
// and what the right-hand axis labels.
//
// These lived in the generic Settings key/value store until this class existed, read as
// `Settings.boolValue("graph/showPressure", true)` in five QML files. That worked, but
// boolValue() is a plain Q_INVOKABLE, so a property bound to it recorded no dependency and
// never updated — every graph was reading a one-shot snapshot taken at construction. Three
// separate compensations grew on top: GraphLegend wrote the setting AND poked the graph's
// property directly, HistoryShotGraph hand-maintained a twelve-case string switch, and
// LastShotChartSource hand-maintained a parallel list of keys it had to watch. That last
// one had already shipped with an entry missing.
//
// As Q_PROPERTYs with NOTIFY, bindings simply work and all three compensations delete.
class SettingsGraph : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap snapshot READ snapshot NOTIFY snapshotChanged FINAL)

    // Trace visibility. Names match the storage keys minus the `graph/` prefix, and match
    // the property names the graphs already used, so QML reads change only in their prefix.
#define GRAPH_PROPERTY(Getter, Setter, Key, Default, Signal) Q_PROPERTY(bool Getter READ Getter WRITE Setter NOTIFY Signal FINAL)
    DECENZA_GRAPH_TRACES(GRAPH_PROPERTY)
#undef GRAPH_PROPERTY

    // Steam visibility is stored separately and must not enter shot-graph menus.
#define STEAM_GRAPH_PROPERTY(Getter, Setter, Key, Default, Signal) Q_PROPERTY(bool Getter READ Getter WRITE Setter NOTIFY Signal FINAL)
    DECENZA_STEAM_GRAPH_TRACES(STEAM_GRAPH_PROPERTY)
#undef STEAM_GRAPH_PROPERTY

    // Reveals the diagnostic curves (resistance, conductance, Darcy R, mix temp) and the
    // advanced panels on the review pages.
    //
    // Storage key stays `shotReview/advancedMode`: it predates this class, and renaming a key
    // silently discards every user's saved value.
    Q_PROPERTY(bool advancedMode READ advancedMode WRITE setAdvancedMode NOTIFY advancedModeChanged FINAL)

    // Factor applied to the flow-family traces before plotting: 1, 2 or 3.
    //
    // Called "multiplier" rather than "flow scale" because Settings::useFlowScale() already
    // means "derive flow from the connected scale" — an unrelated idea sharing every word.
    Q_PROPERTY(int flowMultiplier READ flowMultiplier WRITE setFlowMultiplier NOTIFY flowMultiplierChanged FINAL)

    // Which quantity the right-hand axis labels: "weight", "temperature" or "flow".
    Q_PROPERTY(QString rightAxisMode READ rightAxisMode WRITE setRightAxisMode NOTIFY rightAxisModeChanged FINAL)

public:
    explicit SettingsGraph(QObject* parent = nullptr);
    QVariantMap snapshot() const;

    // The vocabulary lives here so the graphs, the option menus and the tests all spell it
    // the same way. A mode compared against a hand-typed "Flow" in one file fails silently:
    // the resolver just falls back to weight and nothing reports a problem.
    static constexpr int kFlowMultiplierDefault = 2;
    static constexpr int kFlowMultiplierMin = 1;
    static constexpr int kFlowMultiplierMax = 3;
    static constexpr QLatin1String kRightAxisWeight{"weight"};
    static constexpr QLatin1String kRightAxisTemperature{"temperature"};
    static constexpr QLatin1String kRightAxisFlow{"flow"};

    bool showPortalEc() const;
    void setShowPortalEc(bool show);
    bool showPortalTemperature() const;
    void setShowPortalTemperature(bool show);
    bool showPressure() const;
    void setShowPressure(bool show);
    bool showFlow() const;
    void setShowFlow(bool show);
    bool showTemperature() const;
    void setShowTemperature(bool show);
    bool showWeight() const;
    void setShowWeight(bool show);
    bool showWeightFlow() const;
    void setShowWeightFlow(bool show);
    bool showResistance() const;
    void setShowResistance(bool show);
    bool showConductance() const;
    void setShowConductance(bool show);
    bool showConductanceDerivative() const;
    void setShowConductanceDerivative(bool show);
    bool showDarcyResistance() const;
    void setShowDarcyResistance(bool show);
    bool showTemperatureMix() const;
    void setShowTemperatureMix(bool show);
    bool showTemperatureMixGoal() const;
    void setShowTemperatureMixGoal(bool show);

#define STEAM_GRAPH_DECLARATION(Getter, Setter, Key, Default, Signal) bool Getter() const; void Setter(bool show);
    DECENZA_STEAM_GRAPH_TRACES(STEAM_GRAPH_DECLARATION)
#undef STEAM_GRAPH_DECLARATION

    bool advancedMode() const;
    void setAdvancedMode(bool enabled);

    int flowMultiplier() const;
    void setFlowMultiplier(int multiplier);

    QString rightAxisMode() const;
    void setRightAxisMode(const QString& mode);

    // Advance the right-axis mode one step and persist it, returning the new value. Three
    // graphs share this control; the cycle order belongs in one place rather than being
    // re-typed as a ternary chain in each of them.
    Q_INVOKABLE QString cycleRightAxisMode();

    // Static and pure so the rules are testable without a settings store on disk, matching
    // Settings::looksLikeFreshInstall().

    // Anything that is not 1, 2 or 3 — unset, zero, negative, non-numeric, 7 — is the default.
    static int resolveFlowMultiplier(const QVariant& stored);

    // Resolution is read-time only and never writes back, so a device that never opens a
    // graph is never migrated and its superseded value stays intact for a rollback.
    static QString resolveRightAxisMode(const QVariant& storedMode,
                                        const QVariant& legacyShowWeightAxis);

    static QString nextRightAxisMode(const QString& current);

signals:
    void snapshotChanged();
    void showPortalEcChanged();
    void showPortalTemperatureChanged();
    void showPressureChanged();
    void showFlowChanged();
    void showTemperatureChanged();
    void showWeightChanged();
    void showWeightFlowChanged();
    void showResistanceChanged();
    void showConductanceChanged();
    void showConductanceDerivativeChanged();
    void showDarcyResistanceChanged();
    void showTemperatureMixChanged();
    void showTemperatureMixGoalChanged();
#define STEAM_GRAPH_SIGNAL(Getter, Setter, Key, Default, Signal) void Signal();
    DECENZA_STEAM_GRAPH_TRACES(STEAM_GRAPH_SIGNAL)
#undef STEAM_GRAPH_SIGNAL
    void advancedModeChanged();
    void flowMultiplierChanged();
    void rightAxisModeChanged();

private:
    bool readBool(QLatin1String key, bool defaultValue) const;
    void writeBool(QLatin1String key, bool defaultValue, bool value,
                   void (SettingsGraph::*signal)());

    mutable AppSettings m_settings;
};
