#include "settings_dye.h"
#include "settings_visualizer.h"
#include "grinderaliases.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QtMath>

SettingsDye::SettingsDye(SettingsVisualizer* visualizer, QObject* parent)
    : QObject(parent)
    , m_settings("DecentEspresso", "DE1Qt")
    , m_visualizer(visualizer)
{
    // Seed empty bean presets list if missing (user adds their own)
    if (!m_settings.contains("bean/presets")) {
        QJsonArray empty;
        m_settings.setValue("bean/presets", QJsonDocument(empty).toJson());
    }

    // One-time legacy DYE grinder migration: split the combined "model" string
    // into brand/model/burrs using the alias table.
    if (!m_settings.contains("dye/grinderBrand") || m_settings.value("dye/grinderBrand").toString().isEmpty()) {
        QString oldModel = m_settings.value("dye/grinderModel").toString();
        if (!oldModel.isEmpty()) {
            auto result = GrinderAliases::lookup(oldModel);
            if (result.found) {
                m_settings.setValue("dye/grinderBrand", result.brand);
                m_settings.setValue("dye/grinderModel", result.model);
                if (m_settings.value("dye/grinderBurrs").toString().isEmpty()) {
                    m_settings.setValue("dye/grinderBurrs", result.stockBurrs);
                }
                qDebug() << "SettingsDye: Migrated DYE grinder ->"
                         << result.brand << result.model << result.stockBurrs;
            }
        }
    }

    // Beans-modified tracking: recompute whenever any DYE bean/grinder field or
    // the selected preset / preset list changes. Fields tracked here must stay
    // in sync with those compared in recomputeBeansModified() and written by
    // applyBeanPreset(). All sources are internal to this domain — no
    // cross-domain wiring required.
    connect(this, &SettingsDye::dyeBeanBrandChanged,     this, &SettingsDye::recomputeBeansModified);
    connect(this, &SettingsDye::dyeBeanTypeChanged,      this, &SettingsDye::recomputeBeansModified);
    connect(this, &SettingsDye::dyeRoastDateChanged,     this, &SettingsDye::recomputeBeansModified);
    connect(this, &SettingsDye::dyeRoastLevelChanged,    this, &SettingsDye::recomputeBeansModified);
    connect(this, &SettingsDye::dyeGrinderBrandChanged,  this, &SettingsDye::recomputeBeansModified);
    connect(this, &SettingsDye::dyeGrinderModelChanged,  this, &SettingsDye::recomputeBeansModified);
    connect(this, &SettingsDye::dyeGrinderBurrsChanged,  this, &SettingsDye::recomputeBeansModified);
    connect(this, &SettingsDye::dyeGrinderSettingChanged, this, &SettingsDye::recomputeBeansModified);
    connect(this, &SettingsDye::dyeBaristaChanged,       this, &SettingsDye::recomputeBeansModified);
    connect(this, &SettingsDye::selectedBeanPresetChanged, this, &SettingsDye::recomputeBeansModified);
    connect(this, &SettingsDye::beanPresetsChanged,      this, &SettingsDye::recomputeBeansModified);
    recomputeBeansModified();  // Seed initial state from persisted values
}

// DYE metadata

QString SettingsDye::dyeBeanBrand() const {
    return m_settings.value("dye/beanBrand", "").toString();
}

void SettingsDye::setDyeBeanBrand(const QString& value) {
    if (dyeBeanBrand() != value) {
        m_settings.setValue("dye/beanBrand", value);
        emit dyeBeanBrandChanged();
    }
}

QString SettingsDye::dyeBeanType() const {
    return m_settings.value("dye/beanType", "").toString();
}

void SettingsDye::setDyeBeanType(const QString& value) {
    if (dyeBeanType() != value) {
        m_settings.setValue("dye/beanType", value);
        emit dyeBeanTypeChanged();
    }
}

QString SettingsDye::dyeRoastDate() const {
    return m_settings.value("dye/roastDate", "").toString();
}

void SettingsDye::setDyeRoastDate(const QString& value) {
    if (dyeRoastDate() != value) {
        m_settings.setValue("dye/roastDate", value);
        emit dyeRoastDateChanged();
    }
}

QString SettingsDye::dyeRoastLevel() const {
    return m_settings.value("dye/roastLevel", "").toString();
}

void SettingsDye::setDyeRoastLevel(const QString& value) {
    if (dyeRoastLevel() != value) {
        m_settings.setValue("dye/roastLevel", value);
        emit dyeRoastLevelChanged();
    }
}

void SettingsDye::ensureDyeCacheLoaded() const {
    if (!m_dyeCacheInitialized) {
        m_dyeGrinderBrandCache = m_settings.value("dye/grinderBrand", "").toString();
        m_dyeGrinderModelCache = m_settings.value("dye/grinderModel", "").toString();
        m_dyeGrinderBurrsCache = m_settings.value("dye/grinderBurrs", "").toString();
        m_dyeGrinderSettingCache = m_settings.value("dye/grinderSetting", "").toString();
        m_dyeBeanWeightCache = m_settings.value("dye/beanWeight", 18.0).toDouble();
        m_dyeDrinkWeightCache = m_settings.value("dye/drinkWeight", 36.0).toDouble();
        m_dyeCacheInitialized = true;
    }
}

QString SettingsDye::dyeGrinderBrand() const {
    ensureDyeCacheLoaded();
    return m_dyeGrinderBrandCache;
}

void SettingsDye::setDyeGrinderBrand(const QString& value) {
    if (dyeGrinderBrand() != value) {
        m_dyeGrinderBrandCache = value;
        m_settings.setValue("dye/grinderBrand", value);
        emit dyeGrinderBrandChanged();
    }
}

QString SettingsDye::dyeGrinderModel() const {
    ensureDyeCacheLoaded();
    return m_dyeGrinderModelCache;
}

void SettingsDye::setDyeGrinderModel(const QString& value) {
    if (dyeGrinderModel() != value) {
        m_dyeGrinderModelCache = value;
        m_settings.setValue("dye/grinderModel", value);
        emit dyeGrinderModelChanged();
    }
}

QString SettingsDye::dyeGrinderBurrs() const {
    ensureDyeCacheLoaded();
    return m_dyeGrinderBurrsCache;
}

void SettingsDye::setDyeGrinderBurrs(const QString& value) {
    if (dyeGrinderBurrs() != value) {
        m_dyeGrinderBurrsCache = value;
        m_settings.setValue("dye/grinderBurrs", value);
        emit dyeGrinderBurrsChanged();
    }
}

QString SettingsDye::dyeGrinderSetting() const {
    ensureDyeCacheLoaded();
    return m_dyeGrinderSettingCache;
}

void SettingsDye::setDyeGrinderSetting(const QString& value) {
    if (dyeGrinderSetting() != value) {
        m_dyeGrinderSettingCache = value;
        m_settings.setValue("dye/grinderSetting", value);
        emit dyeGrinderSettingChanged();
    }
}

QStringList SettingsDye::suggestedBurrs(const QString& brand, const QString& model) const {
    return GrinderAliases::suggestedBurrs(brand, model);
}

bool SettingsDye::isBurrSwappable(const QString& brand, const QString& model) const {
    return GrinderAliases::isBurrSwappable(brand, model);
}

QStringList SettingsDye::knownGrinderBrands() const {
    return GrinderAliases::allBrands();
}

QStringList SettingsDye::knownGrinderModels(const QString& brand) const {
    return GrinderAliases::modelsForBrand(brand);
}

double SettingsDye::dyeBeanWeight() const {
    ensureDyeCacheLoaded();
    return m_dyeBeanWeightCache;
}

void SettingsDye::setDyeBeanWeight(double value) {
    if (!qFuzzyCompare(1.0 + dyeBeanWeight(), 1.0 + value)) {
        m_dyeBeanWeightCache = value;
        m_settings.setValue("dye/beanWeight", value);
        emit dyeBeanWeightChanged();
    }
}

double SettingsDye::dyeDrinkWeight() const {
    ensureDyeCacheLoaded();
    return m_dyeDrinkWeightCache;
}

void SettingsDye::setDyeDrinkWeight(double value) {
    if (!qFuzzyCompare(1.0 + dyeDrinkWeight(), 1.0 + value)) {
        m_dyeDrinkWeightCache = value;
        m_settings.setValue("dye/drinkWeight", value);
        emit dyeDrinkWeightChanged();
    }
}

double SettingsDye::dyeDrinkTds() const {
    return m_settings.value("dye/drinkTds", 0.0).toDouble();
}

void SettingsDye::setDyeDrinkTds(double value) {
    if (!qFuzzyCompare(1.0 + dyeDrinkTds(), 1.0 + value)) {
        m_settings.setValue("dye/drinkTds", value);
        emit dyeDrinkTdsChanged();
    }
}

double SettingsDye::dyeDrinkEy() const {
    return m_settings.value("dye/drinkEy", 0.0).toDouble();
}

void SettingsDye::setDyeDrinkEy(double value) {
    if (!qFuzzyCompare(1.0 + dyeDrinkEy(), 1.0 + value)) {
        m_settings.setValue("dye/drinkEy", value);
        emit dyeDrinkEyChanged();
    }
}

int SettingsDye::dyeEspressoEnjoyment() const {
    return m_settings.value("dye/espressoEnjoyment", m_visualizer->defaultShotRating()).toInt();
}

void SettingsDye::setDyeEspressoEnjoyment(int value) {
    if (dyeEspressoEnjoyment() != value) {
        m_settings.setValue("dye/espressoEnjoyment", value);
        emit dyeEspressoEnjoymentChanged();
    }
}

QString SettingsDye::dyeShotNotes() const {
    // Try new key first, fall back to old key for backward compatibility
    QString notes = m_settings.value("dye/shotNotes", "").toString();
    if (notes.isEmpty()) {
        notes = m_settings.value("dye/espressoNotes", "").toString();
    }
    return notes;
}

void SettingsDye::setDyeShotNotes(const QString& value) {
    if (dyeShotNotes() != value) {
        m_settings.setValue("dye/shotNotes", value);
        // Clear legacy key so the fallback in the getter doesn't resurrect old notes
        if (value.isEmpty()) {
            m_settings.remove("dye/espressoNotes");
        }
        emit dyeShotNotesChanged();
    }
}

QString SettingsDye::dyeBarista() const {
    return m_settings.value("dye/barista", "").toString();
}

void SettingsDye::setDyeBarista(const QString& value) {
    if (dyeBarista() != value) {
        m_settings.setValue("dye/barista", value);
        emit dyeBaristaChanged();
    }
}

QString SettingsDye::dyeShotDateTime() const {
    return m_settings.value("dye/shotDateTime", "").toString();
}

void SettingsDye::setDyeShotDateTime(const QString& value) {
    if (dyeShotDateTime() != value) {
        m_settings.setValue("dye/shotDateTime", value);
        emit dyeShotDateTimeChanged();
    }
}

// Bean presets

QJsonArray SettingsDye::getBeanPresetsArray() const {
    QByteArray data = m_settings.value("bean/presets").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    return doc.array();
}

QVariantList SettingsDye::beanPresets() const {
    QJsonArray arr = getBeanPresetsArray();

    QVariantList result;
    for (const QJsonValue& v : arr) {
        QVariantMap row = v.toObject().toVariantMap();
        // Backfill legacy presets (no showOnIdle field) — default true to preserve behavior
        if (!row.contains("showOnIdle")) {
            row["showOnIdle"] = true;
        }
        result.append(row);
    }
    return result;
}

int SettingsDye::selectedBeanPreset() const {
    return m_settings.value("bean/selectedPreset", -1).toInt();
}

void SettingsDye::setSelectedBeanPreset(int index) {
    if (selectedBeanPreset() != index) {
        m_settings.setValue("bean/selectedPreset", index);
        emit selectedBeanPresetChanged();
    }
}

void SettingsDye::addBeanPreset(const QString& name, const QString& brand, const QString& type,
                                const QString& roastDate, const QString& roastLevel,
                                const QString& grinderBrand, const QString& grinderModel,
                                const QString& grinderBurrs, const QString& grinderSetting,
                                const QString& barista) {
    QJsonArray arr = getBeanPresetsArray();

    QJsonObject preset;
    preset["name"] = name;
    preset["brand"] = brand;
    preset["type"] = type;
    preset["roastDate"] = roastDate;
    preset["roastLevel"] = roastLevel;
    preset["grinderBrand"] = grinderBrand;
    preset["grinderModel"] = grinderModel;
    preset["grinderBurrs"] = grinderBurrs;
    preset["grinderSetting"] = grinderSetting;
    preset["barista"] = barista;
    preset["showOnIdle"] = true;
    arr.append(preset);

    m_settings.setValue("bean/presets", QJsonDocument(arr).toJson());
    emit beanPresetsChanged();
}

void SettingsDye::updateBeanPreset(int index, const QString& name, const QString& brand,
                                   const QString& type, const QString& roastDate,
                                   const QString& roastLevel, const QString& grinderBrand,
                                   const QString& grinderModel, const QString& grinderBurrs,
                                   const QString& grinderSetting, const QString& barista) {
    QJsonArray arr = getBeanPresetsArray();

    if (index >= 0 && index < static_cast<int>(arr.size())) {
        // Preserve showOnIdle from existing entry (default true for legacy)
        QJsonObject existing = arr[index].toObject();
        bool showOnIdle = existing.contains("showOnIdle") ? existing["showOnIdle"].toBool() : true;

        QJsonObject preset;
        preset["name"] = name;
        preset["brand"] = brand;
        preset["type"] = type;
        preset["roastDate"] = roastDate;
        preset["roastLevel"] = roastLevel;
        preset["grinderBrand"] = grinderBrand;
        preset["grinderModel"] = grinderModel;
        preset["grinderBurrs"] = grinderBurrs;
        preset["grinderSetting"] = grinderSetting;
        preset["barista"] = barista;
        preset["showOnIdle"] = showOnIdle;
        arr[index] = preset;

        m_settings.setValue("bean/presets", QJsonDocument(arr).toJson());
        emit beanPresetsChanged();
    }
}

void SettingsDye::removeBeanPreset(int index) {
    QJsonArray arr = getBeanPresetsArray();

    if (index >= 0 && index < static_cast<int>(arr.size())) {
        arr.removeAt(index);
        m_settings.setValue("bean/presets", QJsonDocument(arr).toJson());

        int selected = selectedBeanPreset();
        if (selected >= static_cast<int>(arr.size()) && arr.size() > 0) {
            setSelectedBeanPreset(static_cast<int>(arr.size()) - 1);
        } else if (arr.size() == 0) {
            setSelectedBeanPreset(-1);
        } else if (selected > index) {
            setSelectedBeanPreset(selected - 1);
        }

        emit beanPresetsChanged();
    }
}

void SettingsDye::moveBeanPreset(int from, int to) {
    QJsonArray arr = getBeanPresetsArray();

    if (from >= 0 && from < static_cast<int>(arr.size()) && to >= 0 && to < static_cast<int>(arr.size()) && from != to) {
        QJsonValue item = arr[from];
        arr.removeAt(from);
        arr.insert(to, item);
        m_settings.setValue("bean/presets", QJsonDocument(arr).toJson());

        int selected = selectedBeanPreset();
        if (selected == from) {
            setSelectedBeanPreset(to);
        } else if (from < selected && to >= selected) {
            setSelectedBeanPreset(selected - 1);
        } else if (from > selected && to <= selected) {
            setSelectedBeanPreset(selected + 1);
        }

        emit beanPresetsChanged();
    }
}

void SettingsDye::setBeanPresetShowOnIdle(int index, bool show) {
    QJsonArray arr = getBeanPresetsArray();

    if (index >= 0 && index < static_cast<int>(arr.size())) {
        QJsonObject preset = arr[index].toObject();
        if (preset["showOnIdle"].toBool(true) != show) {
            preset["showOnIdle"] = show;
            arr[index] = preset;
            m_settings.setValue("bean/presets", QJsonDocument(arr).toJson());
            emit beanPresetsChanged();
        }
    }
}

QVariantList SettingsDye::idleBeanPresets() const {
    QJsonArray arr = getBeanPresetsArray();

    QVariantList result;
    for (qsizetype i = 0; i < arr.size(); ++i) {
        QJsonObject obj = arr[i].toObject();
        bool showOnIdle = obj.contains("showOnIdle") ? obj["showOnIdle"].toBool() : true;
        if (!showOnIdle) continue;
        QVariantMap row = obj.toVariantMap();
        row["showOnIdle"] = true;
        row["originalIndex"] = static_cast<int>(i);
        result.append(row);
    }
    return result;
}

QVariantMap SettingsDye::getBeanPreset(int index) const {
    QJsonArray arr = getBeanPresetsArray();

    if (index >= 0 && index < arr.size()) {
        QVariantMap row = arr[index].toObject().toVariantMap();
        if (!row.contains("showOnIdle")) {
            row["showOnIdle"] = true;
        }
        return row;
    }
    return QVariantMap();
}

void SettingsDye::applyBeanPreset(int index) {
    QVariantMap preset = getBeanPreset(index);
    if (preset.isEmpty()) {
        return;
    }

    // Migrate legacy presets: split combined grinder model using alias lookup
    QString brand = preset.value("grinderBrand").toString();
    QString model = preset.value("grinderModel").toString();
    QString burrs = preset.value("grinderBurrs").toString();
    if (brand.isEmpty() && !model.isEmpty()) {
        auto result = GrinderAliases::lookup(model);
        if (result.found) {
            brand = result.brand;
            model = result.model;
            if (burrs.isEmpty()) burrs = result.stockBurrs;
            // Persist the migration back to the preset
            updateBeanPreset(index, preset.value("name").toString(),
                             preset.value("brand").toString(), preset.value("type").toString(),
                             preset.value("roastDate").toString(), preset.value("roastLevel").toString(),
                             brand, model, burrs,
                             preset.value("grinderSetting").toString(),
                             preset.value("barista").toString());
        }
    }

    // Apply all preset fields to DYE settings
    setDyeBeanBrand(preset.value("brand").toString());
    setDyeBeanType(preset.value("type").toString());
    setDyeRoastDate(preset.value("roastDate").toString());
    setDyeRoastLevel(preset.value("roastLevel").toString());
    setDyeGrinderBrand(brand);
    setDyeGrinderModel(model);
    setDyeGrinderBurrs(burrs);
    setDyeGrinderSetting(preset.value("grinderSetting").toString());
    setDyeBarista(preset.value("barista").toString());
}

void SettingsDye::recomputeBeansModified() {
    bool modified = false;
    const int idx = selectedBeanPreset();
    if (idx >= 0) {
        const QVariantMap preset = getBeanPreset(idx);
        if (!preset.isEmpty()) {
            modified = dyeBeanBrand()      != preset.value("brand").toString()
                    || dyeBeanType()       != preset.value("type").toString()
                    || dyeRoastDate()      != preset.value("roastDate").toString()
                    || dyeRoastLevel()     != preset.value("roastLevel").toString()
                    || dyeGrinderBrand()   != preset.value("grinderBrand").toString()
                    || dyeGrinderModel()   != preset.value("grinderModel").toString()
                    || dyeGrinderBurrs()   != preset.value("grinderBurrs").toString()
                    || dyeGrinderSetting() != preset.value("grinderSetting").toString()
                    || dyeBarista()        != preset.value("barista").toString();
        }
    }
    if (modified != m_beansModified) {
        m_beansModified = modified;
        emit beansModifiedChanged();
    }
}

void SettingsDye::saveBeanPresetFromCurrent(const QString& name) {
    int existingIndex = findBeanPresetByName(name);
    if (existingIndex >= 0) {
        updateBeanPreset(existingIndex,
                         name,
                         dyeBeanBrand(), dyeBeanType(), dyeRoastDate(), dyeRoastLevel(),
                         dyeGrinderBrand(), dyeGrinderModel(), dyeGrinderBurrs(),
                         dyeGrinderSetting(), dyeBarista());
        setSelectedBeanPreset(existingIndex);
    } else {
        addBeanPreset(name,
                      dyeBeanBrand(), dyeBeanType(), dyeRoastDate(), dyeRoastLevel(),
                      dyeGrinderBrand(), dyeGrinderModel(), dyeGrinderBurrs(),
                      dyeGrinderSetting(), dyeBarista());
        setSelectedBeanPreset(static_cast<int>(getBeanPresetsArray().size()) - 1);
    }
}

int SettingsDye::findBeanPresetByContent(const QString& brand, const QString& type) const {
    QJsonArray arr = getBeanPresetsArray();
    for (int i = 0; i < arr.size(); ++i) {
        QJsonObject obj = arr[i].toObject();
        if (obj["brand"].toString() == brand && obj["type"].toString() == type) {
            return i;
        }
    }
    return -1;
}

int SettingsDye::findBeanPresetByName(const QString& name) const {
    QJsonArray arr = getBeanPresetsArray();
    for (int i = 0; i < arr.size(); ++i) {
        QJsonObject obj = arr[i].toObject();
        if (obj["name"].toString() == name) {
            return i;
        }
    }
    return -1;
}
