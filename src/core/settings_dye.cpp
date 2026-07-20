#include "settings_dye.h"
#include "settings.h"
#include "../history/bagid.h"
#include "../history/coffeebagstorage.h"
#include "../history/equipmentstorage.h"
#include "grinderaliases.h"
#include "yieldspec.h"
#include "basketaliases.h"

#include <QtMath>
#include <QDebug>

SettingsDye::SettingsDye(QObject* parent)
    : QObject(parent)
#ifdef DECENZA_TESTING
    , m_settings(Settings::testQSettingsPath(), QSettings::IniFormat)
#else
    , m_settings("DecentEspresso", "DE1Qt")
#endif
{
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
                    const QString storageHint = bag.value("storageHint").toString();
                    const QString opened = bag.value("openedDate").toString();
                    if (frozen != m_activeBagFrozenDate || defrost != m_activeBagDefrostDate
                        || storageHint != m_activeBagStorageHint || opened != m_activeBagOpenedDate) {
                        m_activeBagFrozenDate = frozen;
                        m_activeBagDefrostDate = defrost;
                        m_activeBagStorageHint = storageHint;
                        m_activeBagOpenedDate = opened;
                        emit activeBagChanged();
                    }
                    // Refresh the cached yield spec SILENTLY (no
                    // activeBagYieldSpecApplied): keep-fields means the caller
                    // owns what applies to the session — recipe activation
                    // must not have the bag's spec re-armed over the
                    // recipe's — but the cache must still be truthful, or the
                    // yield-baseline ladder and Brew Settings' Update Bag
                    // button would read the PREVIOUS bag's spec
                    // (add-yield-ratio-anchor).
                    m_activeBagYieldValue = bag.value("yieldValue", 0.0).toDouble();
                    m_activeBagYieldMode = YieldSpec::normalizedMode(bag.value("yieldMode").toString());
                    if (m_activeBagYieldValue <= 0)
                        m_activeBagYieldMode = YieldSpec::modeNone();
                    emit activeBagYieldSpecChanged();
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
                const qint64 id = activeEquipmentId();
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

    // The package's display name (user-editable label; defaults to "{brand}
    // {model}"). Surfaced so UI shows the package name, not the grinder identity.
    QString name = pkg.value("name").toString().trimmed();
    if (name.isEmpty())
        name = (dyeGrinderBrand().trimmed() + QLatin1Char(' ') + dyeGrinderModel().trimmed()).trimmed();
    if (m_dyeEquipmentName != name) {
        m_dyeEquipmentName = name;
        emit dyeEquipmentNameChanged();
    }

    // Basket identity + registry summary (resolved from the package; empty when
    // the package has no basket or it's a custom off-registry basket).
    const QString basketBrand = pkg.value("basketBrand").toString();
    const QString basketModel = pkg.value("basketModel").toString();
    const QString basketSummary = pkg.value("basketSummary").toString();
    if (m_dyeBasketBrand != basketBrand || m_dyeBasketModel != basketModel
        || m_dyeBasketSummary != basketSummary) {
        m_dyeBasketBrand = basketBrand;
        m_dyeBasketModel = basketModel;
        m_dyeBasketSummary = basketSummary;
        emit dyeBasketChanged();
    }

    const QString puckCanon = pkg.value("puckPrepCanonical").toString();
    if (m_dyePuckPrepCanonical != puckCanon) {
        m_dyePuckPrepCanonical = puckCanon;
        emit dyePuckPrepChanged();
    }
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
        // Unconditional, like every other bag-backed dye field: the bag always
        // mirrors the most recently dialed grind (fix-recipe-grind-integrity —
        // the recipe-pin suspension is retired; an active recipe's own grind
        // is stamped independently by MainController off the same edit).
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
        // Unconditional, like grind above — rpm rides with the dial.
        writeThroughToBag("rpm", value > 0 ? QVariant(value) : QVariant());
        writeThroughToActivePackage("lastRpm", value > 0 ? QVariant(value) : QVariant());
        emit dyeGrinderRpmChanged();
    }
}

qint64 SettingsDye::activeEquipmentId() const {
    return m_settings.value("dye/activeEquipmentId", -1).toLongLong();
}

void SettingsDye::setActiveEquipmentId(qint64 id) {
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
    const qint64 id = pkg.value("id").toLongLong();
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
    // Refresh the basket display cache immediately (the async packageReady ->
    // applyEquipmentIdentity will re-confirm it). The picker's pkg map carries the
    // resolved basket identity + summary from EquipmentPackageView::toVariantMap.
    const QString basketBrand = pkg.value("basketBrand").toString();
    const QString basketModel = pkg.value("basketModel").toString();
    const QString basketSummary = pkg.value("basketSummary").toString();
    if (m_dyeBasketBrand != basketBrand || m_dyeBasketModel != basketModel
        || m_dyeBasketSummary != basketSummary) {
        m_dyeBasketBrand = basketBrand;
        m_dyeBasketModel = basketModel;
        m_dyeBasketSummary = basketSummary;
        emit dyeBasketChanged();
    }
    const QString puckCanon = pkg.value("puckPrepCanonical").toString();
    if (m_dyePuckPrepCanonical != puckCanon) {
        m_dyePuckPrepCanonical = puckCanon;
        emit dyePuckPrepChanged();
    }
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

QString SettingsDye::stepGrinderSetting(const QString& brand, const QString& model,
                                        const QString& current, double deltaUnits,
                                        int decimals) const {
    // Registry-only: a custom grinder (no match) returns "" so the caller keeps
    // its plain-numeric / letter / history fallback, exactly as before.
    const GrinderAliases::GrinderEntry* entry = GrinderAliases::findEntry(brand, model);
    if (!entry)
        return QString();
    const auto linear = GrinderAliases::parseGrinderSetting(*entry, current);
    if (!linear)
        return QString();  // unparseable (e.g. pure letters) → caller's fallback
    const double stepped = *linear + deltaUnits;
    if (stepped < 0.0)
        return QString();  // below the dial floor → skip this row

    // Compound rotation ("a+b") renders in its own notation (rev/position
    // carry-borrow). We gate on the CURRENT value actually being compound: a
    // compound-notation grinder whose setting is recorded as a plain number
    // (parseGrinderSetting accepts that — some Mignon users log "0.5") keeps the
    // numeric form instead of being re-notated to "0+0.5".
    if (entry->notation == GrinderAliases::SettingNotation::Compound
        && current.contains(QLatin1Char('+')))
        return GrinderAliases::formatGrinderSetting(*entry, stepped);

    // Plain-numeric: honor the caller's step precision (the grind widget passes
    // the decimals of its history-derived step, up to 2), not
    // formatGrinderSetting's fixed single decimal, then strip trailing zeros to
    // match the display convention.
    const int d = qBound(0, decimals, 3);
    QString s = QString::number(stepped, 'f', d);
    if (s.contains(QLatin1Char('.'))) {
        while (s.endsWith(QLatin1Char('0'))) s.chop(1);
        if (s.endsWith(QLatin1Char('.'))) s.chop(1);
    }
    return s;
}

QStringList SettingsDye::knownBasketBrands() const {
    return BasketAliases::allBrands();
}

QStringList SettingsDye::knownBasketModels(const QString& brand) const {
    return BasketAliases::modelsForBrand(brand);
}

QString SettingsDye::basketModelSummary(const QString& brand, const QString& model) const {
    if (const BasketAliases::BasketEntry* e = BasketAliases::findEntry(brand, model))
        return BasketAliases::summary(*e);
    return QString();
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
        // bag's yield spec — the session anchor lives in Settings.brew and
        // reaches the bag only via persistYieldSpecToBag(). No write-through.
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

int SettingsDye::activeRecipeId() const {
    return m_settings.value("dye/activeRecipeId", -1).toInt();
}

void SettingsDye::setActiveRecipeId(int recipeId) {
    if (activeRecipeId() == recipeId)
        return;
    m_settings.setValue("dye/activeRecipeId", recipeId);
    emit activeRecipeIdChanged();
}

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
        m_activeBagStorageHint.clear();
        m_activeBagOpenedDate.clear();
        m_activeBagYieldValue = 0;      // no bag → no yield spec
        m_activeBagYieldMode = QStringLiteral("none");
        emit activeBagYieldSpecChanged();
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
        m_activeBagStorageHint.clear();
        m_activeBagOpenedDate.clear();
        m_activeBagYieldValue = 0;      // no bag → no yield spec
        m_activeBagYieldMode = QStringLiteral("none");
        emit activeBagYieldSpecChanged();
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
    setActiveEquipmentId(bag.value("equipmentId", -1).toLongLong());
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
    const QString storageHint = bag.value("storageHint").toString();
    const QString opened = bag.value("openedDate").toString();
    if (frozen != m_activeBagFrozenDate || defrost != m_activeBagDefrostDate
        || storageHint != m_activeBagStorageHint || opened != m_activeBagOpenedDate) {
        m_activeBagFrozenDate = frozen;
        m_activeBagDefrostDate = defrost;
        m_activeBagStorageHint = storageHint;
        m_activeBagOpenedDate = opened;
        emit activeBagChanged();
    }

    // The bag's yield spec drives the session anchor (Settings.brew), not the
    // DYE drink weight. Emit last (after m_applyingBag clears) so the
    // MainController applies it on top of the switch's clear-to-profile
    // reset — gated there on no recipe being active (the ladder: recipe
    // outranks bag). mode "none" → the brew stays at the profile default.
    m_activeBagYieldValue = bag.value("yieldValue", 0.0).toDouble();
    m_activeBagYieldMode = YieldSpec::normalizedMode(bag.value("yieldMode").toString());
    if (m_activeBagYieldValue <= 0)
        m_activeBagYieldMode = YieldSpec::modeNone();
    emit activeBagYieldSpecChanged();
    emit activeBagYieldSpecApplied(m_activeBagYieldValue, m_activeBagYieldMode);
}

void SettingsDye::persistYieldSpecToBag(double value, const QString& mode)
{
    // The "Update Bag" write — the only path a yield reaches the bag. Goes
    // through writeThroughToBag per field (skips when no bag/storage is
    // attached, and dodges the bagUpdated echo via m_pendingSelfWrites).
    // Writing a spec whose mode is "none" clears the bag's anchor.
    m_activeBagYieldMode = YieldSpec::normalizedMode(mode);
    // Normalize to the range the session resolves within (YieldSpec::clampValue
    // — 0.5–6.0 for a ratio, 1–500 g for an absolute). This holds the invariant
    // at the point of STORAGE: a bag must never hold a value the session cannot
    // resolve to, or its stored design and the brewed shot disagree permanently
    // and silently — the bag reads 900 g everywhere while the shot pulls 500.
    // Clamp ONLY a value that is actually set: 0 is the "no anchor" sentinel,
    // and clamping it would bound it UP to the 1 g floor and defeat the
    // mode-collapse below.
    const bool haveValue = YieldSpec::isSet(m_activeBagYieldMode) && value > 0;
    m_activeBagYieldValue = haveValue ? YieldSpec::clampValue(m_activeBagYieldMode, value) : 0.0;
    if (m_activeBagYieldValue <= 0)
        m_activeBagYieldMode = YieldSpec::modeNone();
    // Notify unconditionally: the cache above has already changed, so the
    // Q_PROPERTY bindings that read it (the Update Bag button's enabled
    // state, the brew baseline) are stale from here on whether or not a bag
    // is attached to persist to.
    emit activeBagYieldSpecChanged();
    if (!m_applyingBag && m_bagStorage && bagIdIsSet(activeBagId())) {
        m_pendingSelfWrites++;
        m_bagStorage->requestUpdateBag(activeBagId(),
                                       {{QStringLiteral("yieldValue"), m_activeBagYieldValue},
                                        {QStringLiteral("yieldMode"), m_activeBagYieldMode}});
        // The bag's spec is a baseline rung — let listeners (the brew
        // baseline, idle widgets) re-evaluate against the new stored value.
        emit activeBagYieldSpecApplied(m_activeBagYieldValue, m_activeBagYieldMode);
    }
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
    const qint64 eqId = activeEquipmentId();
    if (eqId <= 0)
        return;
    m_equipmentStorage->requestUpdatePackage(eqId, {{field, value}});
}
