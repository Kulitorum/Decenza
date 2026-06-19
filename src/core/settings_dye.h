#pragma once

#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class SettingsVisualizer;
class CoffeeBagStorage;
class EquipmentStorage;

// DYE (Describe Your Espresso) metadata. Split from Settings to keep
// settings.h's transitive-include footprint small. Holds a non-owning
// pointer to SettingsVisualizer so dyeEspressoEnjoyment can fall back to the
// user-configured defaultShotRating when no per-shot value has been written.
//
// Bean model (bean-bag-inventory): the active coffee bag IS the bean state.
// The dye/* QSettings keys act as a synchronous write-through cache of the
// active bag — selecting a bag copies its fields in (applyActiveBag), and
// every bean/grinder/dose setter writes through to the bag row on a
// background thread. There is no separate preset list and no modified-state
// tracking; the old bean/presets QSettings array is migrated to the
// coffee_bags table by ShotHistoryStorage.
class SettingsDye : public QObject {
    Q_OBJECT

    // DYE metadata - sticky fields
    Q_PROPERTY(QString dyeBeanBrand READ dyeBeanBrand WRITE setDyeBeanBrand NOTIFY dyeBeanBrandChanged)
    Q_PROPERTY(QString dyeBeanType READ dyeBeanType WRITE setDyeBeanType NOTIFY dyeBeanTypeChanged)
    Q_PROPERTY(QString dyeRoastDate READ dyeRoastDate WRITE setDyeRoastDate NOTIFY dyeRoastDateChanged)
    Q_PROPERTY(QString dyeRoastLevel READ dyeRoastLevel WRITE setDyeRoastLevel NOTIFY dyeRoastLevelChanged)
    Q_PROPERTY(QString dyeGrinderBrand READ dyeGrinderBrand WRITE setDyeGrinderBrand NOTIFY dyeGrinderBrandChanged)
    Q_PROPERTY(QString dyeGrinderModel READ dyeGrinderModel WRITE setDyeGrinderModel NOTIFY dyeGrinderModelChanged)
    Q_PROPERTY(QString dyeGrinderBurrs READ dyeGrinderBurrs WRITE setDyeGrinderBurrs NOTIFY dyeGrinderBurrsChanged)
    // Active package's display name (read-only; defaults to "{brand} {model}").
    // UI shows this instead of the raw grinder identity (add-equipment-packages).
    Q_PROPERTY(QString dyeEquipmentName READ dyeEquipmentName NOTIFY dyeEquipmentNameChanged)
    // Active package's basket identity (read-only; empty when none). Basket is
    // part of the package identity, not a per-shot dial, so these are resolved
    // from the active package like dyeEquipmentName (add-basket-equipment).
    Q_PROPERTY(QString dyeBasketBrand READ dyeBasketBrand NOTIFY dyeBasketChanged)
    Q_PROPERTY(QString dyeBasketModel READ dyeBasketModel NOTIFY dyeBasketChanged)
    // Active package's puck-prep canonical flag string (e.g. "shaker,wdt"; empty =
    // none), resolved from the active package (add-puckprep-equipment). The Edit
    // Equipment form prefills its checkboxes from this when adding new gear.
    Q_PROPERTY(QString dyePuckPrepCanonical READ dyePuckPrepCanonical NOTIFY dyePuckPrepChanged)
    // One-line registry summary of the active basket (wall/flow/precision/dose),
    // empty for none or a custom off-registry basket.
    Q_PROPERTY(QString dyeBasketSummary READ dyeBasketSummary NOTIFY dyeBasketChanged)
    Q_PROPERTY(QString dyeGrinderSetting READ dyeGrinderSetting WRITE setDyeGrinderSetting NOTIFY dyeGrinderSettingChanged)
    // Grinder rpm dial-in (add-equipment-packages); shown only when the active
    // package's grinder is rpmCapable. 0 = unset.
    Q_PROPERTY(int dyeGrinderRpm READ dyeGrinderRpm WRITE setDyeGrinderRpm NOTIFY dyeGrinderRpmChanged)
    // The active equipment package id (the grinder the next shot is ground on).
    // Switching it applies the package's grinder identity + last dial.
    Q_PROPERTY(qint64 activeEquipmentId READ activeEquipmentId WRITE setActiveEquipmentId NOTIFY activeEquipmentIdChanged)
    Q_PROPERTY(double dyeBeanWeight READ dyeBeanWeight WRITE setDyeBeanWeight NOTIFY dyeBeanWeightChanged)
    Q_PROPERTY(double dyeDrinkWeight READ dyeDrinkWeight WRITE setDyeDrinkWeight NOTIFY dyeDrinkWeightChanged)
    Q_PROPERTY(double dyeDrinkTds READ dyeDrinkTds WRITE setDyeDrinkTds NOTIFY dyeDrinkTdsChanged)
    Q_PROPERTY(double dyeDrinkEy READ dyeDrinkEy WRITE setDyeDrinkEy NOTIFY dyeDrinkEyChanged)
    Q_PROPERTY(int dyeEspressoEnjoyment READ dyeEspressoEnjoyment WRITE setDyeEspressoEnjoyment NOTIFY dyeEspressoEnjoymentChanged)
    Q_PROPERTY(QString dyeShotNotes READ dyeShotNotes WRITE setDyeShotNotes NOTIFY dyeShotNotesChanged)
    Q_PROPERTY(QString dyeBarista READ dyeBarista WRITE setDyeBarista NOTIFY dyeBaristaChanged)
    Q_PROPERTY(QString dyeShotDateTime READ dyeShotDateTime WRITE setDyeShotDateTime NOTIFY dyeShotDateTimeChanged)

    // Bean Base (Loffee Labs) link state — sticky like the bean fields.
    // dyeBeanBaseId empty = unlinked (the free-text-only path most beans use).
    // dyeBeanBaseData holds the full cached entry as one compact-JSON blob;
    // consumers (shot snapshot, Visualizer upload, AI advisor) read it as a
    // unit, so it is deliberately NOT split into per-attribute properties.
    Q_PROPERTY(QString dyeBeanBaseId READ dyeBeanBaseId WRITE setDyeBeanBaseId NOTIFY dyeBeanBaseIdChanged)
    Q_PROPERTY(QString dyeBeanBaseData READ dyeBeanBaseData WRITE setDyeBeanBaseData NOTIFY dyeBeanBaseDataChanged)

    // The active coffee bag (DB row id in coffee_bags), -1 = no bag selected.
    // Setting it loads the bag and applies its fields to the dye cache.
    Q_PROPERTY(int activeBagId READ activeBagId WRITE setActiveBagId NOTIFY activeBagIdChanged)
    // Lifecycle fields of the active bag, mirrored for QML display and the
    // shot snapshot (read-only here; edited via CoffeeBagStorage).
    Q_PROPERTY(QString activeBagFrozenDate READ activeBagFrozenDate NOTIFY activeBagChanged)
    Q_PROPERTY(QString activeBagDefrostDate READ activeBagDefrostDate NOTIFY activeBagChanged)

public:
    // visualizer is non-owning and must outlive this object (Settings owns both).
    explicit SettingsDye(SettingsVisualizer* visualizer, QObject* parent = nullptr);

    // Non-owning; attached by main.cpp once ShotHistoryStorage has run the
    // migrations. Loads the active bag into the dye cache and subscribes to
    // bag updates so external edits (bag edit dialog, Next Portion, dose
    // stamp) refresh the cache.
    void setBagStorage(CoffeeBagStorage* storage);

    // Non-owning; attached by MainController after storage init. The active
    // bag's equipment_id points at a package here; the dye grinder identity is
    // resolved through it (add-equipment-packages).
    void setEquipmentStorage(EquipmentStorage* storage);

    // DYE metadata
    QString dyeBeanBrand() const;
    void setDyeBeanBrand(const QString& value);

    QString dyeBeanType() const;
    void setDyeBeanType(const QString& value);

    QString dyeRoastDate() const;
    void setDyeRoastDate(const QString& value);

    QString dyeRoastLevel() const;
    void setDyeRoastLevel(const QString& value);

    QString dyeGrinderBrand() const;
    void setDyeGrinderBrand(const QString& value);

    QString dyeGrinderModel() const;
    void setDyeGrinderModel(const QString& value);

    QString dyeGrinderBurrs() const;
    void setDyeGrinderBurrs(const QString& value);

    QString dyeEquipmentName() const { return m_dyeEquipmentName; }

    QString dyeBasketBrand() const { return m_dyeBasketBrand; }
    QString dyeBasketModel() const { return m_dyeBasketModel; }
    QString dyeBasketSummary() const { return m_dyeBasketSummary; }
    QString dyePuckPrepCanonical() const { return m_dyePuckPrepCanonical; }

    QString dyeGrinderSetting() const;
    void setDyeGrinderSetting(const QString& value);

    int dyeGrinderRpm() const;
    void setDyeGrinderRpm(int value);

    qint64 activeEquipmentId() const;
    void setActiveEquipmentId(qint64 id);

    // User-facing equipment switch: adopt the package's grinder identity and its
    // last dial (grind setting + rpm), and point the active bag at it. `pkg` is a
    // package map from EquipmentStorage (id, grinderBrand/Model/Burrs,
    // lastGrindSetting, lastRpm).
    Q_INVOKABLE void switchToEquipment(const QVariantMap& pkg);

    // True when the grinder identity is rpm-adjustable (registry variableRpm, or
    // a custom grinder not in the registry). Drives the rpm field's visibility in
    // Brew Settings.
    Q_INVOKABLE bool grinderRpmCapable(const QString& brand, const QString& model) const;

    Q_INVOKABLE QStringList suggestedBurrs(const QString& brand, const QString& model) const;
    Q_INVOKABLE bool isBurrSwappable(const QString& brand, const QString& model) const;
    Q_INVOKABLE QStringList knownGrinderBrands() const;
    Q_INVOKABLE QStringList knownGrinderModels(const QString& brand) const;

    // Basket registry bridges for the vendor-first picker (add-basket-equipment).
    Q_INVOKABLE QStringList knownBasketBrands() const;
    Q_INVOKABLE QStringList knownBasketModels(const QString& brand) const;
    // Differentiator subtitle for a basket model row (registry summary), e.g.
    // "58mm, straight-wall, precision, open flow, 17-19g". Empty for a custom basket.
    Q_INVOKABLE QString basketModelSummary(const QString& brand, const QString& model) const;

    double dyeBeanWeight() const;
    void setDyeBeanWeight(double value);

    double dyeDrinkWeight() const;
    void setDyeDrinkWeight(double value);

    double dyeDrinkTds() const;
    void setDyeDrinkTds(double value);

    double dyeDrinkEy() const;
    void setDyeDrinkEy(double value);

    int dyeEspressoEnjoyment() const;
    void setDyeEspressoEnjoyment(int value);

    QString dyeShotNotes() const;
    void setDyeShotNotes(const QString& value);

    QString dyeBarista() const;
    void setDyeBarista(const QString& value);

    QString dyeShotDateTime() const;
    void setDyeShotDateTime(const QString& value);

    QString dyeBeanBaseId() const;
    void setDyeBeanBaseId(const QString& value);

    QString dyeBeanBaseData() const;
    void setDyeBeanBaseData(const QString& value);

    // Convenience: clear all Bean Base link fields (Unlink action). Writes
    // through to the active bag like any other bean-field edit.
    Q_INVOKABLE void clearBeanBaseLink();

    // Active bag
    int activeBagId() const;
    void setActiveBagId(int bagId);
    // Select a bag WITHOUT copying its stored fields into the dye cache —
    // for callers about to apply field values of their own (loading a
    // historical shot's setup, where the shot's grind/dose should win over
    // the bag's last-used values; the subsequent setters write through, so
    // the bag adopts the shot's values). Lifecycle display fields still
    // refresh.
    Q_INVOKABLE void setActiveBagKeepFields(int bagId);
    QString activeBagFrozenDate() const { return m_activeBagFrozenDate; }
    QString activeBagDefrostDate() const { return m_activeBagDefrostDate; }

    // The active bag's yield override (grams). 0 = no override; the bag
    // follows the profile's target weight. Applied to Settings.brew's
    // brewYieldOverride on a user bean switch (see applyActiveBag); the
    // brew-settings commit writes it back via persistYieldOverrideToBag().
    double activeBagYieldOverrideG() const { return m_activeBagYieldOverrideG; }
    // Persist the brew-settings yield override to the active bag. yieldOverrideG
    // <= 0 stores 0 (no override — the bag follows the profile default). Called
    // from ProfileManager::activateBrewWithOverrides (the single brew-settings
    // commit point), not on every override change, so there is no race with the
    // clear-on-switch reset.
    void persistYieldOverrideToBag(double yieldOverrideG);

    // Invalidate cached DYE values so the next getter re-reads from QSettings.
    // Called by Settings::factoryReset() after wiping the store. Also zeros
    // the session-scratch TDS/EY since they don't live in QSettings.
    void invalidateCache() {
        m_dyeCacheInitialized = false;
        m_dyeDrinkTds = 0.0;
        m_dyeDrinkEy = 0.0;
    }

signals:
    void dyeBeanBrandChanged();
    void dyeBeanTypeChanged();
    void dyeRoastDateChanged();
    void dyeRoastLevelChanged();
    void dyeGrinderBrandChanged();
    void dyeGrinderModelChanged();
    void dyeGrinderBurrsChanged();
    void dyeEquipmentNameChanged();
    void dyeBasketChanged();
    void dyePuckPrepChanged();
    void dyeGrinderSettingChanged();
    void dyeGrinderRpmChanged();
    void activeEquipmentIdChanged();
    void dyeBeanWeightChanged();
    void dyeDrinkWeightChanged();
    void dyeDrinkTdsChanged();
    void dyeDrinkEyChanged();
    void dyeEspressoEnjoymentChanged();
    void dyeShotNotesChanged();
    void dyeBaristaChanged();
    void dyeShotDateTimeChanged();
    void dyeBeanBaseIdChanged();
    void dyeBeanBaseDataChanged();
    void activeBagIdChanged();
    void activeBagChanged();
    // Emitted only from applyActiveBag (a user bean switch, not a keep-fields
    // historical/favorite load) carrying the new bag's yield override. The
    // MainController applies it to Settings.brew after the switch's
    // clear-to-profile-default reset, so a bag with an override turns the idle
    // brew-settings widget yellow and a bag without one stays at the default.
    void activeBagYieldOverrideApplied(double yieldOverrideG);

private:
    void ensureDyeCacheLoaded() const;
    // Copy the bag's fields into the dye cache (bean identity always —
    // including empties, absence is silence; grinder/dose only when the bag
    // has values, so a fresh bag inherits the current setup and adopts it
    // via write-through later).
    void applyActiveBag(const QVariantMap& bag);
    // Queue an async write of one field to the active bag (no-op while
    // applyActiveBag is running or when no bag/storage is attached).
    void writeThroughToBag(const QString& field, const QVariant& value);
    // Queue an async write of one field to the active equipment package (no-op
    // while applyActiveBag is running or when no package/storage is attached).
    void writeThroughToActivePackage(const QString& field, const QVariant& value);
    // Refresh the grinder-identity display cache from a resolved package map.
    void applyEquipmentIdentity(const QVariantMap& pkg);

    mutable QSettings m_settings;
    SettingsVisualizer* m_visualizer = nullptr;  // Non-owning; for default-rating fallback.
    CoffeeBagStorage* m_bagStorage = nullptr;    // Non-owning; attached post-init.
    EquipmentStorage* m_equipmentStorage = nullptr; // Non-owning; attached post-init.

    bool m_applyingBag = false;  // Suppress write-through echo during applyActiveBag
    bool m_keepFieldsOnNextApply = false;  // setActiveBagKeepFields: next bagReady only refreshes lifecycle
    int m_pendingSelfWrites = 0; // Outstanding write-throughs whose bagUpdated echo to skip
    QString m_activeBagFrozenDate;
    QString m_activeBagDefrostDate;
    double m_activeBagYieldOverrideG = 0;  // active bag's yield override (0 = none)

    // Cached DYE values (avoid QSettings::value() → CFPreferences on every QML binding read)
    mutable QString m_dyeGrinderBrandCache;
    mutable QString m_dyeGrinderModelCache;
    mutable QString m_dyeGrinderBurrsCache;
    QString m_dyeEquipmentName;  // active package display name (resolved, not persisted)
    QString m_dyeBasketBrand;    // active package basket identity (resolved, not persisted)
    QString m_dyeBasketModel;
    QString m_dyeBasketSummary;  // registry summary of the active basket (resolved)
    QString m_dyePuckPrepCanonical;  // active package puck-prep canonical string (resolved)
    mutable QString m_dyeGrinderSettingCache;
    mutable int m_dyeGrinderRpmCache = 0;
    mutable double m_dyeBeanWeightCache = 18.0;
    mutable double m_dyeDrinkWeightCache = 36.0;
    mutable bool m_dyeCacheInitialized = false;

    // Per-shot refractometer values. Held in memory only, never persisted —
    // a stale TDS from a previous app session has no business showing up on
    // the next shot. The shot record is the source of truth post-save.
    double m_dyeDrinkTds = 0.0;
    double m_dyeDrinkEy = 0.0;
};
