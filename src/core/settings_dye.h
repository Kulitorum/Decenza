#pragma once

#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QJsonArray>

class SettingsVisualizer;

// DYE (Describe Your Espresso) metadata + bean preset CRUD. Split from Settings
// to keep settings.h's transitive-include footprint small. Holds a non-owning
// pointer to SettingsVisualizer so dyeEspressoEnjoyment can fall back to the
// user-configured defaultShotRating when no per-shot value has been written.
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
    Q_PROPERTY(QString dyeGrinderSetting READ dyeGrinderSetting WRITE setDyeGrinderSetting NOTIFY dyeGrinderSettingChanged)
    Q_PROPERTY(double dyeBeanWeight READ dyeBeanWeight WRITE setDyeBeanWeight NOTIFY dyeBeanWeightChanged)
    Q_PROPERTY(double dyeDrinkWeight READ dyeDrinkWeight WRITE setDyeDrinkWeight NOTIFY dyeDrinkWeightChanged)
    Q_PROPERTY(double dyeDrinkTds READ dyeDrinkTds WRITE setDyeDrinkTds NOTIFY dyeDrinkTdsChanged)
    Q_PROPERTY(double dyeDrinkEy READ dyeDrinkEy WRITE setDyeDrinkEy NOTIFY dyeDrinkEyChanged)
    Q_PROPERTY(int dyeEspressoEnjoyment READ dyeEspressoEnjoyment WRITE setDyeEspressoEnjoyment NOTIFY dyeEspressoEnjoymentChanged)
    Q_PROPERTY(QString dyeShotNotes READ dyeShotNotes WRITE setDyeShotNotes NOTIFY dyeShotNotesChanged)
    Q_PROPERTY(QString dyeBarista READ dyeBarista WRITE setDyeBarista NOTIFY dyeBaristaChanged)
    Q_PROPERTY(QString dyeShotDateTime READ dyeShotDateTime WRITE setDyeShotDateTime NOTIFY dyeShotDateTimeChanged)

    // Bean presets
    Q_PROPERTY(QVariantList beanPresets READ beanPresets NOTIFY beanPresetsChanged)
    // Filtered view of beanPresets: only rows where showOnIdle == true. Each row gains
    // an `originalIndex` field mapping back into beanPresets for selection/apply calls.
    Q_PROPERTY(QVariantList idleBeanPresets READ idleBeanPresets NOTIFY beanPresetsChanged)
    Q_PROPERTY(int selectedBeanPreset READ selectedBeanPreset WRITE setSelectedBeanPreset NOTIFY selectedBeanPresetChanged)
    // True when selectedBeanPreset >= 0 AND any DYE bean/grinder field diverges from the preset's stored value.
    // Mirrors the ProfileManager.profileModified concept so the beans pill can show an "unsaved" indicator.
    Q_PROPERTY(bool beansModified READ beansModified NOTIFY beansModifiedChanged)

public:
    // visualizer is non-owning and must outlive this object (Settings owns both).
    explicit SettingsDye(SettingsVisualizer* visualizer, QObject* parent = nullptr);

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

    QString dyeGrinderSetting() const;
    void setDyeGrinderSetting(const QString& value);

    Q_INVOKABLE QStringList suggestedBurrs(const QString& brand, const QString& model) const;
    Q_INVOKABLE bool isBurrSwappable(const QString& brand, const QString& model) const;
    Q_INVOKABLE QStringList knownGrinderBrands() const;
    Q_INVOKABLE QStringList knownGrinderModels(const QString& brand) const;

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

    // Bean presets
    QVariantList beanPresets() const;
    int selectedBeanPreset() const;
    void setSelectedBeanPreset(int index);

    Q_INVOKABLE void addBeanPreset(const QString& name, const QString& brand, const QString& type,
                                   const QString& roastDate, const QString& roastLevel,
                                   const QString& grinderBrand, const QString& grinderModel,
                                   const QString& grinderBurrs, const QString& grinderSetting,
                                   const QString& barista);
    Q_INVOKABLE void updateBeanPreset(int index, const QString& name, const QString& brand,
                                      const QString& type, const QString& roastDate,
                                      const QString& roastLevel, const QString& grinderBrand,
                                      const QString& grinderModel, const QString& grinderBurrs,
                                      const QString& grinderSetting, const QString& barista);
    Q_INVOKABLE void removeBeanPreset(int index);
    Q_INVOKABLE void moveBeanPreset(int from, int to);
    Q_INVOKABLE void setBeanPresetShowOnIdle(int index, bool show);
    QVariantList idleBeanPresets() const;
    Q_INVOKABLE QVariantMap getBeanPreset(int index) const;
    Q_INVOKABLE void applyBeanPreset(int index);              // Sets all DYE fields from preset
    Q_INVOKABLE void saveBeanPresetFromCurrent(const QString& name);
    Q_INVOKABLE int findBeanPresetByContent(const QString& brand, const QString& type) const;
    Q_INVOKABLE int findBeanPresetByName(const QString& name) const;

    bool beansModified() const { return m_beansModified; }

    // Invalidate cached DYE values so the next getter re-reads from QSettings.
    // Called by Settings::factoryReset() after wiping the store.
    void invalidateCache() { m_dyeCacheInitialized = false; }

signals:
    void dyeBeanBrandChanged();
    void dyeBeanTypeChanged();
    void dyeRoastDateChanged();
    void dyeRoastLevelChanged();
    void dyeGrinderBrandChanged();
    void dyeGrinderModelChanged();
    void dyeGrinderBurrsChanged();
    void dyeGrinderSettingChanged();
    void dyeBeanWeightChanged();
    void dyeDrinkWeightChanged();
    void dyeDrinkTdsChanged();
    void dyeDrinkEyChanged();
    void dyeEspressoEnjoymentChanged();
    void dyeShotNotesChanged();
    void dyeBaristaChanged();
    void dyeShotDateTimeChanged();
    void beanPresetsChanged();
    void selectedBeanPresetChanged();
    void beansModifiedChanged();

private:
    QJsonArray getBeanPresetsArray() const;
    void recomputeBeansModified();
    void ensureDyeCacheLoaded() const;

    mutable QSettings m_settings;
    SettingsVisualizer* m_visualizer = nullptr;  // Non-owning; for default-rating fallback.

    bool m_beansModified = false;

    // Cached DYE values (avoid QSettings::value() → CFPreferences on every QML binding read)
    mutable QString m_dyeGrinderBrandCache;
    mutable QString m_dyeGrinderModelCache;
    mutable QString m_dyeGrinderBurrsCache;
    mutable QString m_dyeGrinderSettingCache;
    mutable double m_dyeBeanWeightCache = 18.0;
    mutable double m_dyeDrinkWeightCache = 36.0;
    mutable bool m_dyeCacheInitialized = false;
};
