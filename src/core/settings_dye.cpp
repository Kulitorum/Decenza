#include "settings_dye.h"
#include "../history/bagid.h"
#include "../history/coffeebagstorage.h"
#include "../history/equipmentstorage.h"
#include "settings_visualizer.h"
#include "grinderaliases.h"

#include <QtMath>
#include <QDebug>

SettingsDye::SettingsDye(SettingsVisualizer* visualizer, QObject* parent)
    : QObject(parent)
    , m_settings("DecentEspresso", "DE1Qt")
    , m_visualizer(visualizer)
{
    // The visualizer pointer is required — dyeEspressoEnjoyment() falls back
    // to defaultShotRating() when no per-shot value is persisted, and a null
    // visualizer would crash the getter on first read. Settings::Settings()
    // always passes a fully-constructed instance.
    Q_ASSERT(m_visualizer);

    // NOTE: do NOT seed bean/presets here. The legacy preset array is
    // consumed by ShotHistoryStorage::importLegacyBeanPresets() — re-seeding
    // an empty array would just create churn for the import to retire.

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
}

void SettingsDye::setBagStorage(CoffeeBagStorage* storage)
{
    if (m_bagStorage == storage)
        return;
    m_bagStorage = storage;
    if (!m_bagStorage)
        return;

    // Refresh the dye cache whenever the active bag row changes elsewhere
    // (bag edit dialog, Next Portion, post-shot dose stamp). The apply is a
    // no-op when values are already equal, so the write-through echo settles.
    connect(m_bagStorage, &CoffeeBagStorage::bagReady, this,
            [this](qint64 bagId, const QVariantMap& bag) {
                if (bagId != activeBagId())
                    return;
                if (bag.isEmpty()) {
                    // Active bag vanished (deleted row / failed migration map).
                    qWarning() << "SettingsDye: active bag" << bagId << "not found - clearing selection";
                    m_keepFieldsOnNextApply = false;
                    setActiveBagId(-1);
                    return;
                }
                if (m_keepFieldsOnNextApply) {
                    m_keepFieldsOnNextApply = false;
                    const QString frozen = bag.value("frozenDate").toString();
                    const QString defrost = bag.value("defrostDate").toString();
                    if (frozen != m_activeBagFrozenDate || defrost != m_activeBagDefrostDate) {
                        m_activeBagFrozenDate = frozen;
                        m_activeBagDefrostDate = defrost;
                        emit activeBagChanged();
                    }
                    return;
                }
                applyActiveBag(bag);
            });
    connect(m_bagStorage, &CoffeeBagStorage::bagUpdated, this,
            [this](qint64 bagId, bool success) {
                // Only the active bag concerns the dye cache. Gating here keeps
                // an update to a DIFFERENT bag (a shot-save dose/yield stamp on a
                // non-active bag, a concurrent edit elsewhere) from spuriously
                // consuming a pending-self-write token — write-throughs only ever
                // target the active bag, so a non-active bagUpdated is never our echo.
                if (bagId != activeBagId())
                    return;
                // Skip the refresh for our own write-throughs — the dye cache
                // is already the source of those values; re-reading per
                // keystroke would just churn. (requestUpdateBag always emits
                // bagUpdated, success or fail, so the token is always consumed.)
                if (m_pendingSelfWrites > 0) {
                    m_pendingSelfWrites--;
                    return;
                }
                if (success)
                    m_bagStorage->requestBag(bagId);
            });

    if (bagIdIsSet(activeBagId()))
        m_bagStorage->requestBag(activeBagId());
}

void SettingsDye::setEquipmentStorage(EquipmentStorage* storage)
{
    if (m_equipmentStorage == storage)
        return;
    m_equipmentStorage = storage;
    if (!m_equipmentStorage)
        return;

    // The active package is the source of truth for grinder identity; resolve it
    // here and refresh the display cache whenever it arrives or changes.
    connect(m_equipmentStorage, &EquipmentStorage::packageReady, this,
            [this](qint64 packageId, const QVariantMap& pkg) {
                if (packageId != activeEquipmentId())
                    return;
                applyEquipmentIdentity(pkg);  // empty pkg (not found) clears it
            });
    connect(m_equipmentStorage, &EquipmentStorage::packagesChanged, this,
            [this]() {
                // An edit to the active package (incl. a copy-on-write that left
                // its identity in place) — re-resolve the current selection.
                const int id = activeEquipmentId();
                if (id > 0)
                    m_equipmentStorage->requestPackage(id);
            });

    if (activeEquipmentId() > 0)
        m_equipmentStorage->requestPackage(activeEquipmentId());
}

// Refresh the grinder-identity display cache from a resolved package map (from
// EquipmentStorage). Display-only: the setters update the cache + QSettings
// mirror, never the bag.
void SettingsDye::applyEquipmentIdentity(const QVariantMap& pkg)
{
    setDyeGrinderBrand(pkg.value("grinderBrand").toString());
    setDyeGrinderModel(pkg.value("grinderModel").toString());
    setDyeGrinderBurrs(pkg.value("grinderBurrs").toString());
}

// DYE metadata

QString SettingsDye::dyeBeanBrand() const {
    return m_settings.value("dye/beanBrand", "").toString();
}

void SettingsDye::setDyeBeanBrand(const QString& value) {
    if (dyeBeanBrand() != value) {
        m_settings.setValue("dye/beanBrand", value);
        writeThroughToBag("roasterName", value);
        emit dyeBeanBrandChanged();
    }
}

QString SettingsDye::dyeBeanType() const {
    return m_settings.value("dye/beanType", "").toString();
}

void SettingsDye::setDyeBeanType(const QString& value) {
    if (dyeBeanType() != value) {
        m_settings.setValue("dye/beanType", value);
        writeThroughToBag("coffeeName", value);
        emit dyeBeanTypeChanged();
    }
}

QString SettingsDye::dyeRoastDate() const {
    return m_settings.value("dye/roastDate", "").toString();
}

void SettingsDye::setDyeRoastDate(const QString& value) {
    if (dyeRoastDate() != value) {
        m_settings.setValue("dye/roastDate", value);
        writeThroughToBag("roastDate", value);
        emit dyeRoastDateChanged();
    }
}

QString SettingsDye::dyeRoastLevel() const {
    return m_settings.value("dye/roastLevel", "").toString();
}

void SettingsDye::setDyeRoastLevel(const QString& value) {
    if (dyeRoastLevel() != value) {
        m_settings.setValue("dye/roastLevel", value);
        writeThroughToBag("roastLevel", value);
        emit dyeRoastLevelChanged();
    }
}

void SettingsDye::ensureDyeCacheLoaded() const {
    if (!m_dyeCacheInitialized) {
        m_dyeGrinderBrandCache = m_settings.value("dye/grinderBrand", "").toString();
        m_dyeGrinderModelCache = m_settings.value("dye/grinderModel", "").toString();
        m_dyeGrinderBurrsCache = m_settings.value("dye/grinderBurrs", "").toString();
        m_dyeGrinderSettingCache = m_settings.value("dye/grinderSetting", "").toString();
        m_dyeGrinderRpmCache = m_settings.value("dye/grinderRpm", 0).toInt();
        m_dyeBeanWeightCache = m_settings.value("dye/beanWeight", 18.0).toDouble();
        m_dyeDrinkWeightCache = m_settings.value("dye/drinkWeight", 36.0).toDouble();
        m_dyeCacheInitialized = true;
    }
}

QString SettingsDye::dyeGrinderBrand() const {
    ensureDyeCacheLoaded();
    return m_dyeGrinderBrandCache;
}

// Grinder identity (brand/model/burrs) is RESOLVED from the active equipment
// package (add-equipment-packages), not stored on the bag. These setters update
// the display cache (+ a QSettings mirror for correct cold-start before the
// package resolves) and emit — they do NOT write through to the bag. The cache
// is refreshed from the package by applyEquipmentIdentity() whenever the active
// package changes; callers wanting to *change* identity go through the equipment
// package (switchToEquipment / equipment_update), not these setters.
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
        writeThroughToBag("grinderSetting", value);
        // Dual write-through: the grind setting is grinder-scoped too, so keep
        // the active package's last dial current (add-equipment-packages).
        writeThroughToActivePackage("lastGrindSetting", value);
        emit dyeGrinderSettingChanged();
    }
}

int SettingsDye::dyeGrinderRpm() const {
    ensureDyeCacheLoaded();
    return m_dyeGrinderRpmCache;
}

void SettingsDye::setDyeGrinderRpm(int value) {
    if (dyeGrinderRpm() != value) {
        m_dyeGrinderRpmCache = value;
        m_settings.setValue("dye/grinderRpm", value);
        writeThroughToBag("rpm", value > 0 ? QVariant(value) : QVariant());
        writeThroughToActivePackage("lastRpm", value > 0 ? QVariant(value) : QVariant());
        emit dyeGrinderRpmChanged();
    }
}

int SettingsDye::activeEquipmentId() const {
    return m_settings.value("dye/activeEquipmentId", -1).toInt();
}

void SettingsDye::setActiveEquipmentId(int id) {
    if (activeEquipmentId() == id)
        return;
    m_settings.setValue("dye/activeEquipmentId", id);
    // The active bag adopts the package (skipped during applyActiveBag via the
    // m_applyingBag guard inside writeThroughToBag).
    writeThroughToBag("equipmentId", id > 0 ? QVariant(id) : QVariant());
    emit activeEquipmentIdChanged();
    // Resolve the package to refresh the grinder-identity display cache.
    if (id > 0 && m_equipmentStorage)
        m_equipmentStorage->requestPackage(id);   // -> packageReady -> applyEquipmentIdentity
    else if (id <= 0)
        applyEquipmentIdentity({});               // no equipment -> clear identity
}

void SettingsDye::switchToEquipment(const QVariantMap& pkg) {
    const int id = pkg.value("id").toInt();
    if (id <= 0)
        return;
    setActiveEquipmentId(id);
    // Apply the package's grinder identity to the display cache. The setters
    // cache in QSettings only (no bag write-through); identity is owned by the
    // equipment package and re-resolved from it on activeEquipmentId changes.
    setDyeGrinderBrand(pkg.value("grinderBrand").toString());
    setDyeGrinderModel(pkg.value("grinderModel").toString());
    setDyeGrinderBurrs(pkg.value("grinderBurrs").toString());
    // Apply the package's last dial — grinder-scoped memory, never blank, editable.
    setDyeGrinderSetting(pkg.value("lastGrindSetting").toString());
    setDyeGrinderRpm(pkg.value("lastRpm").toInt());
    if (m_equipmentStorage)
        m_equipmentStorage->requestTouchLastUsed(id);
}

bool SettingsDye::grinderRpmCapable(const QString& brand, const QString& model) const {
    return EquipmentStorage::deriveRpmCapable(brand, model);
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
        writeThroughToBag("doseWeightG", value);
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
        // dyeDrinkWeight is the recorded drink weight (DYE metadata), not the
        // bag's yield override — the override lives in Settings.brew and is
        // persisted to the bag via persistYieldOverrideToBag(). No write-through.
        emit dyeDrinkWeightChanged();
    }
}

double SettingsDye::dyeDrinkTds() const {
    return m_dyeDrinkTds;
}

void SettingsDye::setDyeDrinkTds(double value) {
    if (!qFuzzyCompare(1.0 + m_dyeDrinkTds, 1.0 + value)) {
        m_dyeDrinkTds = value;
        emit dyeDrinkTdsChanged();
    }
}

double SettingsDye::dyeDrinkEy() const {
    return m_dyeDrinkEy;
}

void SettingsDye::setDyeDrinkEy(double value) {
    if (!qFuzzyCompare(1.0 + m_dyeDrinkEy, 1.0 + value)) {
        m_dyeDrinkEy = value;
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
        // When the user clears notes, also remove the legacy key — otherwise
        // the getter's fallback would resurrect old notes from `dye/espressoNotes`.
        // For non-empty writes the new key shadows the old one in the getter, so
        // the legacy key is harmless dead data until the user clears notes.
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

// Bean Base link state

QString SettingsDye::dyeBeanBaseId() const {
    return m_settings.value("dye/beanBaseId", "").toString();
}

void SettingsDye::setDyeBeanBaseId(const QString& value) {
    if (dyeBeanBaseId() != value) {
        m_settings.setValue("dye/beanBaseId", value);
        writeThroughToBag("beanBaseId", value);
        emit dyeBeanBaseIdChanged();
    }
}

QString SettingsDye::dyeBeanBaseData() const {
    return m_settings.value("dye/beanBaseData", "").toString();
}

void SettingsDye::setDyeBeanBaseData(const QString& value) {
    if (dyeBeanBaseData() != value) {
        m_settings.setValue("dye/beanBaseData", value);
        writeThroughToBag("beanBaseData", value);
        emit dyeBeanBaseDataChanged();
    }
}

void SettingsDye::clearBeanBaseLink() {
    setDyeBeanBaseId(QString());
    setDyeBeanBaseData(QString());
}

// Active bag

int SettingsDye::activeBagId() const {
    return m_settings.value("dye/activeBagId", -1).toInt();
}

void SettingsDye::setActiveBagId(int bagId) {
    if (activeBagId() == bagId)
        return;
    m_settings.setValue("dye/activeBagId", bagId);
    emit activeBagIdChanged();

    if (!bagIdIsSet(bagId)) {
        // No bag selected: bean identity goes silent. Grinder/dose globals
        // stay — the physical grinder didn't change.
        m_applyingBag = true;
        setDyeBeanBrand(QString());
        setDyeBeanType(QString());
        setDyeRoastDate(QString());
        setDyeRoastLevel(QString());
        setDyeBeanBaseId(QString());
        setDyeBeanBaseData(QString());
        m_applyingBag = false;
        m_activeBagFrozenDate.clear();
        m_activeBagDefrostDate.clear();
        m_activeBagYieldOverrideG = 0;  // no bag → no override
        emit activeBagChanged();
        return;
    }

    if (m_bagStorage) {
        m_bagStorage->requestBag(bagId);     // applies via bagReady
        m_bagStorage->requestTouchLastUsed(bagId);
    }
}

void SettingsDye::setActiveBagKeepFields(int bagId)
{
    if (activeBagId() == bagId)
        return;
    if (!bagIdIsSet(bagId)) {
        // Caller is about to set its own field values — just drop the bag
        // link without the identity-clearing that setActiveBagId(-1) does.
        m_settings.setValue("dye/activeBagId", -1);
        m_activeBagFrozenDate.clear();
        m_activeBagDefrostDate.clear();
        emit activeBagIdChanged();
        emit activeBagChanged();
        return;
    }
    m_settings.setValue("dye/activeBagId", bagId);
    emit activeBagIdChanged();
    if (m_bagStorage) {
        m_keepFieldsOnNextApply = true;
        m_bagStorage->requestBag(bagId);     // lifecycle-only via bagReady
        m_bagStorage->requestTouchLastUsed(bagId);
    }
}

void SettingsDye::applyActiveBag(const QVariantMap& bag)
{
    // Guard suppresses the setters' write-through — these values just came
    // FROM the bag.
    m_applyingBag = true;

    // Bean identity always follows the bag, including empties — an absent
    // roast date displays as silence, not as the previous bean's date.
    setDyeBeanBrand(bag.value("roasterName").toString());
    setDyeBeanType(bag.value("coffeeName").toString());
    setDyeRoastDate(bag.value("roastDate").toString());
    setDyeRoastLevel(bag.value("roastLevel").toString());
    setDyeBeanBaseId(bag.value("beanBaseId").toString());
    setDyeBeanBaseData(bag.value("beanBaseData").toString());

    // Grinder IDENTITY is not on the bag — it resolves from the equipment
    // package the bag points at (equipment_id). Grind setting + rpm are the
    // bag's bean-scoped dial-in; apply when present (a fresh bag with none keeps
    // the current dial and adopts it on the next edit / shot stamp). Guarded by
    // m_applyingBag so none of this writes back to the bag/package.
    setActiveEquipmentId(bag.value("equipmentId", -1).toInt());
    const QString grindSetting = bag.value("grinderSetting").toString();
    if (!grindSetting.isEmpty())
        setDyeGrinderSetting(grindSetting);
    const int bagRpm = bag.value("rpm", 0).toInt();
    if (bagRpm > 0)
        setDyeGrinderRpm(bagRpm);
    const double dose = bag.value("doseWeightG", 0.0).toDouble();
    if (dose > 0)
        setDyeBeanWeight(dose);

    m_applyingBag = false;

    const QString frozen = bag.value("frozenDate").toString();
    const QString defrost = bag.value("defrostDate").toString();
    if (frozen != m_activeBagFrozenDate || defrost != m_activeBagDefrostDate) {
        m_activeBagFrozenDate = frozen;
        m_activeBagDefrostDate = defrost;
        emit activeBagChanged();
    }

    // The bag's yield override drives Settings.brew, not the DYE drink weight.
    // Emit last (after m_applyingBag clears) so the MainController applies it
    // to brewYieldOverride on top of the switch's clear-to-profile reset. 0 =
    // no override → the brew stays at the profile default the clear restored.
    m_activeBagYieldOverrideG = bag.value("yieldOverrideG", 0.0).toDouble();
    emit activeBagYieldOverrideApplied(m_activeBagYieldOverrideG);
}

void SettingsDye::persistYieldOverrideToBag(double yieldOverrideG)
{
    // Store 0 for "no override" so the bag follows the profile default when
    // re-selected. Goes through writeThroughToBag (skips when no bag/storage
    // is attached, and dodges the bagUpdated echo via m_pendingSelfWrites).
    m_activeBagYieldOverrideG = yieldOverrideG > 0 ? yieldOverrideG : 0.0;
    writeThroughToBag("yieldOverrideG", m_activeBagYieldOverrideG);
}

void SettingsDye::writeThroughToBag(const QString& field, const QVariant& value)
{
    if (m_applyingBag || !m_bagStorage)
        return;
    const int bagId = activeBagId();
    if (!bagIdIsSet(bagId))
        return;
    m_pendingSelfWrites++;
    m_bagStorage->requestUpdateBag(bagId, {{field, value}});
}

void SettingsDye::writeThroughToActivePackage(const QString& field, const QVariant& value)
{
    if (m_applyingBag || !m_equipmentStorage)
        return;
    const int eqId = activeEquipmentId();
    if (eqId <= 0)
        return;
    m_equipmentStorage->requestUpdatePackage(eqId, {{field, value}});
}
