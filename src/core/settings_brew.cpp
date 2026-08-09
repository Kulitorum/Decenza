#include "settings_brew.h"
#include "settings.h"
#include "yieldspec.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtMath>

SettingsBrew::SettingsBrew(QObject* parent)
    : QObject(parent)
{
    // Seed default steam pitcher presets if none exist
    if (!m_settings.contains("steam/pitcherPresets")) {
        QJsonArray defaults;
        QJsonObject small;
        small["name"] = "Small";
        small["duration"] = 30;
        small["flow"] = 150;  // 1.5 ml/s
        defaults.append(small);

        QJsonObject large;
        large["name"] = "Large";
        large["duration"] = 60;
        large["flow"] = 150;  // 1.5 ml/s
        defaults.append(large);

        m_settings.setValue("steam/pitcherPresets", QJsonDocument(defaults).toJson());
    }

    // Seed default water vessel presets if none exist
    if (!m_settings.contains("water/vesselPresets")) {
        QJsonArray defaults;
        QJsonObject vessel;
        vessel["name"] = "Cup";
        vessel["volume"] = 200;
        defaults.append(vessel);

        QJsonObject mug;
        mug["name"] = "Mug";
        mug["volume"] = 350;
        defaults.append(mug);

        m_settings.setValue("water/vesselPresets", QJsonDocument(defaults).toJson());
    }

    // Seed default flush presets if none exist
    if (!m_settings.contains("flush/presets")) {
        QJsonArray defaults;
        QJsonObject quick;
        quick["name"] = "Quick";
        quick["flow"] = 6.0;
        quick["seconds"] = 3.0;
        defaults.append(quick);

        QJsonObject normal;
        normal["name"] = "Normal";
        normal["flow"] = 6.0;
        normal["seconds"] = 5.0;
        defaults.append(normal);

        QJsonObject thorough;
        thorough["name"] = "Thorough";
        thorough["flow"] = 6.0;
        thorough["seconds"] = 10.0;
        defaults.append(thorough);

        m_settings.setValue("flush/presets", QJsonDocument(defaults).toJson());
    }

    // One-time migration: weight-timed steaming is now a GLOBAL seconds-per-gram
    // rate instead of per-pitcher reference milk. The rate assumes a consistent
    // steam flow across pitchers (presets may still override flow — see the honest
    // note in the SteamPage UI); it is a simpler one-calibration model, not a
    // physical guarantee. Existing calibrated users had (calibMilkG, duration)
    // stored per preset; seed the global rate from the first preset that has both
    // so they aren't reset. Runs after the preset-seeding block above so the presets
    // are present. Done in the ctor (not the getter) to avoid a const getter mutating
    // QSettings mid-read.
    //
    // The run-once gate is the SENTINEL, not a "rate <= 0" check. Legacy calibMilkG
    // is left in storage (old backups still carry it, and it re-seeds the rate on
    // import — see importFromJson), so a bare "rate <= 0" guard would re-seed the old
    // rate every launch and silently undo a user who deliberately uncalibrated (the
    // new ± control allows 0). The inner "rate <= 0" check below is a one-time
    // don't-clobber-an-existing-rate guard, distinct from the run-once gate.
    if (!m_settings.value("steam/steamRateMigrated", false).toBool()) {
        if (m_settings.value("steam/steamSecondsPerGram", 0.0).toDouble() <= 0.0) {
            double seeded = deriveSteamRateFromLegacyPresets();
            if (seeded > 0.0) m_settings.setValue("steam/steamSecondsPerGram", seeded);
        }
        m_settings.setValue("steam/steamRateMigrated", true);
    }

    // One-time migration: user-created "Off" pitchers are gone, replaced by the
    // single built-in "Heater off" entry every install now has. Runs after the
    // preset-seeding block so the array is present.
    //
    // The selection remap is the load-bearing half. A user whose resting state
    // was an Off preset has that preset REMOVED here, and a selection left
    // pointing at a stale index resolves to a real pitcher — so without the
    // remap the upgrade silently turns their steam boiler on and leaves it on.
    // The names of what was removed are kept for the recipe rewrite, which needs
    // a database and therefore cannot happen here.
    if (!m_settings.value("steam/heaterOffPresetsMigrated", false).toBool()) {
        const QJsonArray arr = readPresetArray(QStringLiteral("steam/pitcherPresets"));
        QJsonArray kept;
        QStringList removedNames;
        const int selected = m_settings.value("steam/selectedPitcher", 0).toInt();
        int remapped = selected;
        bool selectionWasRemoved = false;
        for (int i = 0; i < arr.size(); ++i) {
            const QJsonObject preset = arr.at(i).toObject();
            // `disabled` is the only flag a user-created Off preset ever carried:
            // addSteamPitcherPresetDisabled() (deleted by this change) wrote that
            // and nothing else. An earlier version of this loop also accepted an
            // `off` key, justified in a comment as "what the add-dialog wrote" —
            // no writer in this codebase's history has ever produced it.
            if (preset.value(QStringLiteral("disabled")).toBool()) {
                removedNames << preset.value(QStringLiteral("name")).toString();
                if (i == selected)
                    selectionWasRemoved = true;
                else if (i < selected)
                    --remapped;   // survivors below the selection shift up by one
                continue;
            }
            kept.append(preset);
        }
        if (!removedNames.isEmpty()) {
            m_settings.setValue("steam/pitcherPresets", QJsonDocument(kept).toJson());
            m_settings.setValue("steam/selectedPitcher",
                                selectionWasRemoved ? HeaterOffPitcherIndex : remapped);
            m_settings.setValue("steam/heaterOffRemovedNames", removedNames);
            qInfo() << "SettingsBrew: replaced" << removedNames.size()
                    << "user Off pitcher preset(s) with the built-in Heater off entry;"
                    << "selection" << selected << "->"
                    << (selectionWasRemoved ? HeaterOffPitcherIndex : remapped);
        }
        m_settings.setValue("steam/heaterOffPresetsMigrated", true);
    }

    // Load persistent brew overrides into the cache (Settings used to do this).
    m_hasTemperatureOverride = m_settings.value("brew/hasTemperatureOverride", false).toBool();
    if (m_hasTemperatureOverride) {
        m_temperatureOverride = m_settings.value("brew/temperatureOverride", 0.0).toDouble();
    }

    // Session yield anchor (add-yield-ratio-anchor): {value, mode}. An absent
    // mode key is a pre-anchor install — migrate on read: "absolute" when the
    // legacy hasBrewYieldOverride flag was set, else "none". No SQL-style
    // migration step (QSettings).
    {
        const QString storedMode = m_settings.value("espresso/brewYieldMode").toString();
        if (storedMode.isEmpty()) {
            m_brewYieldMode = m_settings.value("brew/hasBrewYieldOverride", false).toBool()
                ? YieldSpec::modeAbsolute() : YieldSpec::modeNone();
        } else {
            m_brewYieldMode = YieldSpec::normalizedMode(storedMode);
        }
        if (YieldSpec::isSet(m_brewYieldMode))
            m_brewYieldOverride = m_settings.value("brew/brewYieldOverride", 0.0).toDouble();
        if (m_brewYieldOverride <= 0)
            m_brewYieldMode = YieldSpec::modeNone();
    }
}

// Espresso

double SettingsBrew::espressoTemperature() const {
    return m_settings.value("espresso/temperature", 93.0).toDouble();
}

void SettingsBrew::setEspressoTemperature(double temp) {
    if (espressoTemperature() != temp) {
        m_settings.setValue("espresso/temperature", temp);
        emit espressoTemperatureChanged();
    }
}

double SettingsBrew::targetWeight() const {
    return m_settings.value("espresso/targetWeight", 36.0).toDouble();
}

void SettingsBrew::setTargetWeight(double weight) {
    if (targetWeight() != weight) {
        m_settings.setValue("espresso/targetWeight", weight);
        emit targetWeightChanged();
    }
}

double SettingsBrew::lastUsedRatio() const {
    return m_settings.value("espresso/lastUsedRatio", 2.0).toDouble();
}

void SettingsBrew::setLastUsedRatio(double ratio) {
    if (lastUsedRatio() != ratio) {
        m_settings.setValue("espresso/lastUsedRatio", ratio);
        emit lastUsedRatioChanged();
    }
}

double SettingsBrew::ratioPreset1() const {
    return m_settings.value("espresso/ratioPreset1", 1.0).toDouble();
}
void SettingsBrew::setRatioPreset1(double r) {
    r = YieldSpec::clampRatio(r);
    if (ratioPreset1() != r) {
        m_settings.setValue("espresso/ratioPreset1", r);
        emit ratioPreset1Changed();
    }
}

double SettingsBrew::ratioPreset2() const {
    return m_settings.value("espresso/ratioPreset2", 2.0).toDouble();
}
void SettingsBrew::setRatioPreset2(double r) {
    r = YieldSpec::clampRatio(r);
    if (ratioPreset2() != r) {
        m_settings.setValue("espresso/ratioPreset2", r);
        emit ratioPreset2Changed();
    }
}

double SettingsBrew::ratioPreset3() const {
    return m_settings.value("espresso/ratioPreset3", 3.0).toDouble();
}
void SettingsBrew::setRatioPreset3(double r) {
    r = YieldSpec::clampRatio(r);
    if (ratioPreset3() != r) {
        m_settings.setValue("espresso/ratioPreset3", r);
        emit ratioPreset3Changed();
    }
}

double SettingsBrew::doseCupTareWeight() const {
    return m_settings.value("espresso/doseCupTareWeight", 0.0).toDouble();
}

void SettingsBrew::setDoseCupTareWeight(double weight) {
    if (weight < 0) weight = 0;  // a tare is never negative
    if (doseCupTareWeight() != weight) {
        m_settings.setValue("espresso/doseCupTareWeight", weight);
        emit doseCupTareWeightChanged();
    }
}

bool SettingsBrew::milkAutoCaptureEnabled() const {
    return m_settings.value("steam/milkAutoCaptureEnabled", false).toBool();  // off by default; calibrating turns it on
}
void SettingsBrew::setMilkAutoCaptureEnabled(bool enabled) {
    if (milkAutoCaptureEnabled() != enabled) {
        m_settings.setValue("steam/milkAutoCaptureEnabled", enabled);
        emit milkAutoCaptureEnabledChanged();
    }
}

double SettingsBrew::lastSteamMilkG() const {
    return m_settings.value("steam/lastSteamMilkG", 0.0).toDouble();
}
void SettingsBrew::setLastSteamMilkG(double g) {
    if (g < 0) g = 0;
    if (lastSteamMilkG() != g) {
        m_settings.setValue("steam/lastSteamMilkG", g);
        emit lastSteamMilkGChanged();
    }
}

double SettingsBrew::lastSteamTimeS() const {
    return m_settings.value("steam/lastSteamTimeS", 0.0).toDouble();
}
void SettingsBrew::setLastSteamTimeS(double s) {
    if (s < 0) s = 0;
    if (lastSteamTimeS() != s) {
        m_settings.setValue("steam/lastSteamTimeS", s);
        emit lastSteamTimeSChanged();
    }
}

double SettingsBrew::steamSecondsPerGram() const {
    return m_settings.value("steam/steamSecondsPerGram", 0.0).toDouble();
}
void SettingsBrew::setSteamSecondsPerGram(double secPerGram) {
    if (secPerGram < 0) secPerGram = 0;  // 0 = uncalibrated; never negative
    if (!qFuzzyCompare(1.0 + steamSecondsPerGram(), 1.0 + secPerGram)) {
        m_settings.setValue("steam/steamSecondsPerGram", secPerGram);
        emit steamSecondsPerGramChanged();
    }
}

void SettingsBrew::calibrateSteamFromReference(double milkG, double timeSec) {
    if (milkG <= 0.0 || timeSec <= 0.0) return;  // guarded — need both to derive a rate
    setSteamSecondsPerGram(timeSec / milkG);
    // Weight-timed steaming is off by default; calibrating is the explicit opt-in,
    // mirroring the old per-pitcher setSteamPitcherCalibration behaviour.
    setMilkAutoCaptureEnabled(true);
}

bool SettingsBrew::nameTakenIn(const QJsonArray& arr, const QString& name, int ignoreIndex) {
    const QString wanted = name.trimmed();
    if (wanted.isEmpty()) return false;   // emptiness is rejected by the callers, separately
    for (int i = 0; i < arr.size(); ++i) {
        if (i == ignoreIndex) continue;   // renaming a preset to its own name is not a clash
        if (arr.at(i).toObject().value("name").toString().trimmed()
                .compare(wanted, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

bool SettingsBrew::waterVesselNameTaken(const QString& name, int ignoreIndex) const {
    return nameTakenIn(readPresetArray(QStringLiteral("water/vesselPresets")), name, ignoreIndex);
}

bool SettingsBrew::steamPitcherNameTaken(const QString& name, int ignoreIndex) const {
    // The built-in "Heater off" entry's label is reserved too. It has no stored
    // name to collide with — the view translates it — so the list check below
    // cannot see it, and a user pitcher called "Heater off" would put two rows
    // with that label in every picker. Only the English label is reserved: it is
    // the one the built-in falls back to and the one a user would type.
    if (name.trimmed().compare(QStringLiteral("Heater off"), Qt::CaseInsensitive) == 0)
        return true;
    return nameTakenIn(readPresetArray(QStringLiteral("steam/pitcherPresets")), name, ignoreIndex);
}

QJsonArray SettingsBrew::readPresetArray(const QString& key, bool* parseFailed) const {
    if (parseFailed) *parseFailed = false;
    const QByteArray data = m_settings.value(key).toByteArray();
    // Genuinely unset (or explicitly emptied) — not a failure, just no presets.
    if (data.isEmpty()) return QJsonArray();

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) {
        if (parseFailed) *parseFailed = true;
        // noquote so the key reads as a settings path rather than "a/b" — this
        // line is the only notice a user or a bug report ever gets that their
        // presets became unreadable.
        qWarning().noquote() << "SettingsBrew: could not parse" << key << "-" << err.errorString()
                             << "at offset" << err.offset
                             << "- reporting no presets and refusing to overwrite the stored value";
        return QJsonArray();
    }
    // Valid JSON that is not an array is equally unusable, and equally must not
    // be written over.
    if (!doc.isArray()) {
        if (parseFailed) *parseFailed = true;
        qWarning().noquote() << "SettingsBrew:" << key << "holds valid JSON that is not an array"
                             << "- reporting no presets and refusing to overwrite the stored value";
        return QJsonArray();
    }
    return doc.array();
}

double SettingsBrew::deriveSteamRateFromLegacyPresets() const {
    // Recover a global seconds-per-gram rate from the pre-migration per-pitcher
    // (calibMilkG, duration): the first preset carrying both wins. Returns 0 when no
    // preset was calibrated. Shared by the one-time ctor migration and the backup-
    // import re-seed so both derive the rate identically.
    const QJsonArray arr = readPresetArray(QStringLiteral("steam/pitcherPresets"));
    for (const QJsonValue& v : arr) {
        QJsonObject preset = v.toObject();
        double calibMilk = preset.value("calibMilkG").toDouble(0.0);
        double duration  = preset.value("duration").toDouble(0.0);
        if (calibMilk > 0.0 && duration > 0.0) return duration / calibMilk;
    }
    return 0.0;
}

void SettingsBrew::seedSteamRateFromLegacyPresets() {
    // Only seed a positive derived rate — never clobber the caller's rate with 0 when
    // no legacy calibration is present. Goes through the setter so QML is notified.
    double seeded = deriveSteamRateFromLegacyPresets();
    if (seeded > 0.0) setSteamSecondsPerGram(seeded);
}

bool SettingsBrew::doseCaptureSoundEnabled() const {
    return m_settings.value("espresso/doseCaptureSoundEnabled", false).toBool();
}
void SettingsBrew::setDoseCaptureSoundEnabled(bool enabled) {
    if (doseCaptureSoundEnabled() != enabled) {
        m_settings.setValue("espresso/doseCaptureSoundEnabled", enabled);
        emit doseCaptureSoundEnabledChanged();
    }
}

// Steam

double SettingsBrew::steamTemperature() const {
    return m_settings.value("steam/temperature", 160.0).toDouble();
}

void SettingsBrew::setSteamTemperature(double temp) {
    if (steamTemperature() != temp) {
        m_settings.setValue("steam/temperature", temp);
        emit steamTemperatureChanged();
    }
}

int SettingsBrew::steamTimeout() const {
    return m_settings.value("steam/timeout", 120).toInt();
}

void SettingsBrew::setSteamTimeout(int timeout) {
    if (steamTimeout() != timeout) {
        m_settings.setValue("steam/timeout", timeout);
        emit steamTimeoutChanged();
    }
}

int SettingsBrew::steamFlow() const {
    return m_settings.value("steam/flow", 150).toInt();  // 150 = 1.5 ml/s (range: 40-250)
}

void SettingsBrew::setSteamFlow(int flow) {
    if (steamFlow() != flow) {
        m_settings.setValue("steam/flow", flow);
        emit steamFlowChanged();
    }
}

bool SettingsBrew::steamDisabled() const {
    return m_steamDisabled;
}

void SettingsBrew::setSteamDisabled(bool disabled) {
    if (m_steamDisabled != disabled) {
        m_steamDisabled = disabled;
        emit steamDisabledChanged();
    }
}

bool SettingsBrew::keepWarmWhenIdle() const {
    // Same key as the old single setting: this half IS what it meant, so an
    // existing user's choice carries over with no migration step of its own.
    return m_settings.value("steam/keepHeaterOn", true).toBool();
}

void SettingsBrew::setKeepWarmWhenIdle(bool keep) {
    if (keepWarmWhenIdle() != keep) {
        m_settings.setValue("steam/keepHeaterOn", keep);
        emit keepWarmWhenIdleChanged();
    }
}

bool SettingsBrew::letRecipeDecide() const {
    // Defaults ON, for existing installs as well as fresh ones. That is a
    // deliberate behaviour change at upgrade — nearly every espresso recipe has
    // no pitcher, so a user with one parked will find the heater off where it
    // used to be warm. Shipping it off would deliver the feature only to people
    // who go looking for a setting, which for a heater policy is nearly nobody.
    return m_settings.value("steam/letRecipeDecide", true).toBool();
}

void SettingsBrew::setLetRecipeDecide(bool let) {
    if (letRecipeDecide() != let) {
        m_settings.setValue("steam/letRecipeDecide", let);
        emit letRecipeDecideChanged();
    }
}

int SettingsBrew::steamAutoFlushSeconds() const {
    return m_settings.value("steam/autoFlushSeconds", 0).toInt();
}

void SettingsBrew::setSteamAutoFlushSeconds(int seconds) {
    if (steamAutoFlushSeconds() != seconds) {
        m_settings.setValue("steam/autoFlushSeconds", seconds);
        emit steamAutoFlushSecondsChanged();
    }
}

// Steam pitcher presets

QVariantList SettingsBrew::steamPitcherPresets() const {
    QJsonArray arr = readPresetArray(QStringLiteral("steam/pitcherPresets"));

    QVariantList result;
    for (const QJsonValue& v : arr) {
        result.append(v.toObject().toVariantMap());
    }
    result.append(heaterOffPitcherEntry());
    return result;
}

// The built-in "Heater off" entry. Synthetic — assembled here, never stored.
//
// `disabled` is the flag every existing reader already tests (the policy, the
// pill rows, SteamPage, the wizard), and it is what a user-created Off preset
// carried. `builtin` is what marks this one unmodifiable.
//
// No translated name: this map is data, and the view translates the label. A
// stored name would be stale after a language change, and worse, name is what
// recipes historically matched a pitcher on.
QVariantMap SettingsBrew::heaterOffPitcherEntry() {
    QVariantMap entry;
    entry.insert(QStringLiteral("disabled"), true);
    entry.insert(QStringLiteral("builtin"), true);
    return entry;
}

bool SettingsBrew::isHeaterOffPitcher(int index) const {
    if (index == HeaterOffPitcherIndex)
        return true;
    // The built-in also answers to its display position (the slot past the last
    // stored preset), because the pill rows address delegates by index.
    return index == steamPitcherCount();
}

SettingsBrew::PitcherApply SettingsBrew::applySteamPitcherValues(int index, double milkG) {
    const QVariantMap preset = getSteamPitcherPreset(index);
    if (preset.isEmpty())
        return PitcherApply::Missing;

    if (preset.value(QStringLiteral("disabled")).toBool()) {
        // The built-in "Heater off" entry. Deliberately writes nothing: it
        // carries no duration, flow or temperature, and steaming with it selected
        // falls back to the live settings — which still hold the last real
        // pitcher's values. Writing its (absent) fields would zero them.
        return PitcherApply::HeaterOff;
    }

    setSteamTimeout(effectiveSteamDurationSec(index, milkG));
    if (preset.contains(QStringLiteral("flow")))
        setSteamFlow(preset.value(QStringLiteral("flow")).toInt());
    if (preset.contains(QStringLiteral("temperature")))
        setSteamTemperature(preset.value(QStringLiteral("temperature")).toDouble());
    return PitcherApply::Applied;
}

int SettingsBrew::steamPitcherCount() const {
    return static_cast<int>(readPresetArray(QStringLiteral("steam/pitcherPresets")).size());
}

int SettingsBrew::selectedSteamPitcher() const {
    return m_settings.value("steam/selectedPitcher", 0).toInt();
}

void SettingsBrew::setSelectedSteamCup(int index) {
    // Normalise the built-in to its SENTINEL, never its display slot.
    //
    // The pill rows address delegates by position, so a tap on "Heater off"
    // arrives here as steamPitcherCount() — and that value stops meaning
    // "Heater off" the moment a preset is added or deleted. Worse,
    // removeSteamPitcherPreset() clamps an out-of-range selection down to the
    // last real pitcher, so deleting a preset silently turned a deliberate
    // "keep the boiler cold" into a real pitcher and warmed it.
    //
    // Normalising HERE rather than at each caller is the point: isHeaterOffPitcher()
    // deliberately answers to both representations so callers can pass either,
    // which is exactly what makes an un-normalised store ambiguous. One seam.
    const int stored = isHeaterOffPitcher(index) ? HeaterOffPitcherIndex : index;
    if (selectedSteamPitcher() != stored) {
        m_settings.setValue("steam/selectedPitcher", stored);
        emit selectedSteamPitcherChanged();
    }
}

void SettingsBrew::adjustStandingPitcherForRemoval(int removedIndex) {
    // The standing pitcher is a parked SELECTION, so it needs the same index
    // maintenance the live selection gets. Without it, deleting a preset while a
    // recipe override is active unwinds to a different pitcher — or, when the
    // stale index lands on steamPitcherCount(), to the built-in, so the user's
    // standing pitcher comes back as a cold boiler.
    const int standing = standingSteamPitcher();
    if (standing < 0)
        return;                                   // sentinel or unset: positionless
    if (standing == removedIndex)
        setStandingSteamPitcher(HeaterOffPitcherIndex);  // it IS what was deleted
    else if (standing > removedIndex)
        setStandingSteamPitcher(standing - 1);
}

int SettingsBrew::standingSteamPitcher() const {
    return m_settings.value("steam/standingPitcher", NoStandingPitcher).toInt();
}

void SettingsBrew::setStandingSteamPitcher(int index) {
    m_settings.setValue("steam/standingPitcher", index);
}

int SettingsBrew::resolveRecipePitcherOverride(int recipeIndex) {
    if (recipeIndex != NoStandingPitcher) {
        // Park the standing selection the FIRST time a recipe overrides it. The
        // guard is what makes recipe-to-recipe switching safe: without it, the
        // second activation would park the first recipe's pitcher as if the user
        // had chosen it, and deactivating would restore a drink, not a setting.
        if (standingSteamPitcher() == NoStandingPitcher)
            setStandingSteamPitcher(selectedSteamPitcher());
        return recipeIndex;
    }

    const int standing = standingSteamPitcher();
    if (standing == NoStandingPitcher)
        return NoStandingPitcher;   // no override was ever applied
    setStandingSteamPitcher(NoStandingPitcher);
    return standing;
}

QStringList SettingsBrew::migratedHeaterOffNames() const {
    return m_settings.value("steam/heaterOffRemovedNames").toStringList();
}

void SettingsBrew::clearMigratedHeaterOffNames() {
    m_settings.remove("steam/heaterOffRemovedNames");
}

void SettingsBrew::addSteamPitcherPreset(const QString& name, int duration, int flow, double temperature) {
    bool parseFailed = false;
    QJsonArray arr = readPresetArray(QStringLiteral("steam/pitcherPresets"), &parseFailed);
    // Refuse to append to an array we could not read: writing back would
    // save an empty list over the user's unreadable presets.
    if (parseFailed) return;

    // A preset is addressed BY NAME by everything downstream — recipes snapshot
    // the vessel/pitcher by name and re-select it on activation, and the recipe
    // wizard matches its tiles on it. Two presets sharing a name therefore both
    // light up in the wizard and activation resolves to whichever comes first,
    // so the clash is refused at the setter, where every caller inherits it.
    // Existing duplicates already in storage are left alone: this rejects new
    // ones, it does not delete anyone's data.
    if (nameTakenIn(arr, name, -1)) {
        qWarning() << "SettingsBrew: refusing a duplicate steam pitcher named" << name
                   << "- that name is already in use";
        return;
    }

    QJsonObject preset;
    preset["name"] = name;
    preset["duration"] = duration;
    preset["flow"] = flow;
    preset["temperature"] = temperature;
    arr.append(preset);

    m_settings.setValue("steam/pitcherPresets", QJsonDocument(arr).toJson());
    emit steamPitcherPresetsChanged();
}

void SettingsBrew::updateSteamPitcherPreset(int index, const QString& name, int duration, int flow, double temperature) {
    // The built-in "Heater off" entry is not in the stored array, so it cannot
    // be addressed here. Refusing explicitly rather than letting the index fall
    // through: its display slot is one past the last stored preset, and an
    // off-by-one there would silently edit or delete a real pitcher.
    if (isHeaterOffPitcher(index)) {
        qWarning() << "SettingsBrew: the built-in Heater off entry cannot be modified";
        return;
    }
    bool parseFailed = false;
    QJsonArray arr = readPresetArray(QStringLiteral("steam/pitcherPresets"), &parseFailed);
    // Refuse to append to an array we could not read: writing back would
    // save an empty list over the user's unreadable presets.
    if (parseFailed) return;

    // A preset is addressed BY NAME by everything downstream — recipes snapshot
    // the vessel/pitcher by name and re-select it on activation, and the recipe
    // wizard matches its tiles on it. Two presets sharing a name therefore both
    // light up in the wizard and activation resolves to whichever comes first,
    // so the clash is refused at the setter, where every caller inherits it.
    // Existing duplicates already in storage are left alone: this rejects new
    // ones, it does not delete anyone's data.
    if (nameTakenIn(arr, name, index)) {
        qWarning() << "SettingsBrew: refusing a duplicate steam pitcher named" << name
                   << "- that name is already in use";
        return;
    }

    if (index >= 0 && index < static_cast<int>(arr.size())) {
        QJsonObject preset = arr[index].toObject();  // Read existing to preserve pitcherWeightG / calibMilkG
        preset["name"] = name;
        preset["duration"] = duration;
        preset["flow"] = flow;
        preset["temperature"] = temperature;
        arr[index] = preset;

        m_settings.setValue("steam/pitcherPresets", QJsonDocument(arr).toJson());
        emit steamPitcherPresetsChanged();
    }
}

void SettingsBrew::removeSteamPitcherPreset(int index) {
    // The built-in "Heater off" entry is not in the stored array, so it cannot
    // be addressed here. Refusing explicitly rather than letting the index fall
    // through: its display slot is one past the last stored preset, and an
    // off-by-one there would silently edit or delete a real pitcher.
    if (isHeaterOffPitcher(index)) {
        qWarning() << "SettingsBrew: the built-in Heater off entry cannot be modified";
        return;
    }
    bool parseFailed = false;
    QJsonArray arr = readPresetArray(QStringLiteral("steam/pitcherPresets"), &parseFailed);
    // Refuse to append to an array we could not read: writing back would
    // save an empty list over the user's unreadable presets.
    if (parseFailed) return;

    if (index >= 0 && index < arr.size()) {
        arr.removeAt(index);
        m_settings.setValue("steam/pitcherPresets", QJsonDocument(arr).toJson());

        // Both the live selection and the parked standing pitcher are positions
        // into the array that just shrank, so both must SHIFT, not merely be
        // clamped. Clamping alone was the pre-existing behaviour and it silently
        // moved the selection to a different pitcher: delete index 0 while index
        // 2 is selected and the survivors renumber, so 2 now names what used to
        // be 3. The sentinel is positionless and is left alone by the `>= 0`
        // guards — which is what keeps a deliberate "Heater off" from being
        // clamped into a real pitcher and warming the boiler.
        const int selected = selectedSteamPitcher();
        if (selected == index)
            setSelectedSteamCup(HeaterOffPitcherIndex);   // the selected one was deleted
        else if (selected > index)
            setSelectedSteamCup(selected - 1);
        adjustStandingPitcherForRemoval(index);

        emit steamPitcherPresetsChanged();
    }
}

void SettingsBrew::moveSteamPitcherPreset(int from, int to) {
    // Neither end may be the built-in entry — it is not in the stored array and
    // has no position to move to or from.
    if (isHeaterOffPitcher(from) || isHeaterOffPitcher(to)) {
        qWarning() << "SettingsBrew: the built-in Heater off entry cannot be reordered";
        return;
    }
    bool parseFailed = false;
    QJsonArray arr = readPresetArray(QStringLiteral("steam/pitcherPresets"), &parseFailed);
    // Refuse to append to an array we could not read: writing back would
    // save an empty list over the user's unreadable presets.
    if (parseFailed) return;

    if (from >= 0 && from < arr.size() && to >= 0 && to < arr.size() && from != to) {
        QJsonValue item = arr[from];
        arr.removeAt(from);
        arr.insert(to, item);
        m_settings.setValue("steam/pitcherPresets", QJsonDocument(arr).toJson());

        int selected = selectedSteamPitcher();
        if (selected == from) {
            setSelectedSteamCup(to);
        } else if (from < selected && to >= selected) {
            setSelectedSteamCup(selected - 1);
        } else if (from > selected && to <= selected) {
            setSelectedSteamCup(selected + 1);
        }

        emit steamPitcherPresetsChanged();
    }
}

void SettingsBrew::setSteamPitcherWeight(int index, double weightG) {
    // The built-in "Heater off" entry is not in the stored array, so it cannot
    // be addressed here. Refusing explicitly rather than letting the index fall
    // through: its display slot is one past the last stored preset, and an
    // off-by-one there would silently edit or delete a real pitcher.
    if (isHeaterOffPitcher(index)) {
        qWarning() << "SettingsBrew: the built-in Heater off entry cannot be modified";
        return;
    }
    bool parseFailed = false;
    QJsonArray arr = readPresetArray(QStringLiteral("steam/pitcherPresets"), &parseFailed);
    // Refuse to append to an array we could not read: writing back would
    // save an empty list over the user's unreadable presets.
    if (parseFailed) return;

    if (index >= 0 && index < static_cast<int>(arr.size())) {
        QJsonObject preset = arr[index].toObject();
        preset["pitcherWeightG"] = weightG;
        arr[index] = preset;
        m_settings.setValue("steam/pitcherPresets", QJsonDocument(arr).toJson());
        emit steamPitcherPresetsChanged();
    }
}

void SettingsBrew::setSteamPitcherCalibration(int index, double calibMilkG) {
    // The built-in "Heater off" entry is not in the stored array, so it cannot
    // be addressed here. Refusing explicitly rather than letting the index fall
    // through: its display slot is one past the last stored preset, and an
    // off-by-one there would silently edit or delete a real pitcher.
    if (isHeaterOffPitcher(index)) {
        qWarning() << "SettingsBrew: the built-in Heater off entry cannot be modified";
        return;
    }
    bool parseFailed = false;
    QJsonArray arr = readPresetArray(QStringLiteral("steam/pitcherPresets"), &parseFailed);
    // Refuse to append to an array we could not read: writing back would
    // save an empty list over the user's unreadable presets.
    if (parseFailed) return;

    if (index >= 0 && index < static_cast<int>(arr.size())) {
        QJsonObject preset = arr[index].toObject();
        if (calibMilkG > 0) {
            preset["calibMilkG"] = calibMilkG;
        } else {
            preset.remove("calibMilkG");  // 0 / negative clears the calibration
        }
        arr[index] = preset;
        m_settings.setValue("steam/pitcherPresets", QJsonDocument(arr).toJson());
        emit steamPitcherPresetsChanged();
        // Weight-timed steaming is off by default; setting a reference is the explicit
        // opt-in, so calibrating turns it on. Clearing the reference leaves it as-is.
        if (calibMilkG > 0)
            setMilkAutoCaptureEnabled(true);
    }
}

double SettingsBrew::netMilkForPitcher(int index, double scaleReading) const {
    QVariantMap p = getSteamPitcherPreset(index);
    if (p.isEmpty() || p.value("disabled").toBool()) return 0.0;
    double pitcherWt = p.value("pitcherWeightG", 0.0).toDouble();
    // One consistent net-milk rule: require a saved empty-pitcher weight (same gate as
    // auto-capture), so there's no tare-vs-saved ambiguity. Net milk = scale − pitcher.
    if (pitcherWt <= 0.0) return 0.0;
    double milk = scaleReading - pitcherWt;
    return (milk >= 50.0 && milk <= 1500.0) ? milk : 0.0;
}

int SettingsBrew::scaledSteamTime(int index, double milkG) const {
    if (!milkAutoCaptureEnabled()) return 0;  // toggle gates ALL weight scaling, not just auto-capture
    QVariantMap p = getSteamPitcherPreset(index);
    if (p.isEmpty() || p.value("disabled").toBool()) return 0;
    if (milkG <= 0.0) return 0;
    // One global seconds-per-gram rate for every pitcher (assumes a consistent steam
    // flow — a simplification, not a physical guarantee; see the SteamPage note). No
    // longer reads the per-preset calibMilkG/duration for scaling.
    double secPerGram = steamSecondsPerGram();
    if (secPerGram <= 0.0) return 0;  // uncalibrated
    return qBound(5, qRound(secPerGram * milkG), 120);
}

int SettingsBrew::effectiveSteamDurationSec(int index, double milkG) const {
    int t = scaledSteamTime(index, milkG);
    if (t > 0) return t;
    QVariantMap p = getSteamPitcherPreset(index);
    if (p.isEmpty()) {
        // A stale/out-of-range selection index (e.g. every preset deleted). Unlike a
        // disabled preset, this is never a deliberate state — warn so the resulting
        // 0s steam timeout is diagnosable.
        qWarning() << "SettingsBrew: no steam pitcher preset at index" << index
                   << "— steam timeout will be 0s";
        return 0;
    }
    if (p.value("disabled").toBool()) return 0;
    int base = qRound(p.value("duration", 0.0).toDouble());
    // An enabled preset with no positive duration is corrupt (hand-edited/failed import);
    // returning 0 here silently programs a 0s steam target, so make it loud.
    if (base <= 0)
        qWarning() << "SettingsBrew: enabled steam pitcher preset" << index
                   << "has no positive duration — steam timeout will be 0s";
    return base;
}

QVariantMap SettingsBrew::getSteamPitcherPreset(int index) const {
    QJsonArray arr = readPresetArray(QStringLiteral("steam/pitcherPresets"));

    if (index >= 0 && index < arr.size()) {
        return arr[index].toObject().toVariantMap();
    }
    // The built-in "Heater off" entry: its sentinel, and its display slot just
    // past the last stored preset. Everything else out of range stays empty.
    if (index == HeaterOffPitcherIndex || index == static_cast<int>(arr.size()))
        return heaterOffPitcherEntry();
    return QVariantMap();
}

// Hot water

double SettingsBrew::waterTemperature() const {
    return m_settings.value("water/temperature", 85.0).toDouble();
}

void SettingsBrew::setWaterTemperature(double temp) {
    if (waterTemperature() != temp) {
        m_settings.setValue("water/temperature", temp);
        emit waterTemperatureChanged();
    }
}

int SettingsBrew::waterVolume() const {
    return m_settings.value("water/volume", 200).toInt();
}

void SettingsBrew::setWaterVolume(int volume) {
    if (waterVolume() != volume) {
        m_settings.setValue("water/volume", volume);
        emit waterVolumeChanged();
    }
}

QString SettingsBrew::waterVolumeMode() const {
    return m_settings.value("water/volumeMode", "weight").toString();
}

void SettingsBrew::setWaterVolumeMode(const QString& mode) {
    if (waterVolumeMode() != mode) {
        m_settings.setValue("water/volumeMode", mode);
        emit waterVolumeModeChanged();
    }
}

int SettingsBrew::effectiveHotWaterVolume() const {
    if (waterVolumeMode() != "volume") return 0;
    return qBound(0, waterVolume(), 255);  // BLE uint8 range
}

double SettingsBrew::hotWaterSawOffset() const {
    return m_settings.value("water/sawOffset", 2.0).toDouble();
}

void SettingsBrew::setHotWaterSawOffset(double offset) {
    if (!qFuzzyCompare(hotWaterSawOffset(), offset)) {
        m_settings.setValue("water/sawOffset", offset);
        emit hotWaterSawOffsetChanged();
    }
}

int SettingsBrew::hotWaterSawSampleCount() const {
    return m_settings.value("water/sawSampleCount", 0).toInt();
}

void SettingsBrew::setHotWaterSawSampleCount(int count) {
    if (hotWaterSawSampleCount() != count) {
        m_settings.setValue("water/sawSampleCount", count);
        emit hotWaterSawSampleCountChanged();
    }
}

// Hot water vessel presets

QVariantList SettingsBrew::waterVesselPresets() const {
    QJsonArray arr = readPresetArray(QStringLiteral("water/vesselPresets"));

    QVariantList result;
    for (const QJsonValue& v : arr) {
        result.append(v.toObject().toVariantMap());
    }
    return result;
}

int SettingsBrew::selectedWaterVessel() const {
    return m_settings.value("water/selectedVessel", 0).toInt();
}

void SettingsBrew::setSelectedWaterCup(int index) {
    if (selectedWaterVessel() != index) {
        m_settings.setValue("water/selectedVessel", index);
        emit selectedWaterVesselChanged();
    }
}

void SettingsBrew::addWaterVesselPreset(const QString& name, int volume, const QString& mode, int flowRate, double temperature) {
    bool parseFailed = false;
    QJsonArray arr = readPresetArray(QStringLiteral("water/vesselPresets"), &parseFailed);
    // Refuse to append to an array we could not read: writing back would
    // save an empty list over the user's unreadable presets.
    if (parseFailed) return;

    // A preset is addressed BY NAME by everything downstream — recipes snapshot
    // the vessel/pitcher by name and re-select it on activation, and the recipe
    // wizard matches its tiles on it. Two presets sharing a name therefore both
    // light up in the wizard and activation resolves to whichever comes first,
    // so the clash is refused at the setter, where every caller inherits it.
    // Existing duplicates already in storage are left alone: this rejects new
    // ones, it does not delete anyone's data.
    if (nameTakenIn(arr, name, -1)) {
        qWarning() << "SettingsBrew: refusing a duplicate water vessel named" << name
                   << "- that name is already in use";
        return;
    }

    QJsonObject preset;
    preset["name"] = name;
    preset["volume"] = volume;
    preset["mode"] = mode;
    preset["flowRate"] = flowRate;
    preset["temperature"] = temperature;
    arr.append(preset);

    m_settings.setValue("water/vesselPresets", QJsonDocument(arr).toJson());
    emit waterVesselPresetsChanged();
}

void SettingsBrew::updateWaterVesselPreset(int index, const QString& name, int volume, const QString& mode, int flowRate, double temperature) {
    bool parseFailed = false;
    QJsonArray arr = readPresetArray(QStringLiteral("water/vesselPresets"), &parseFailed);
    // Refuse to append to an array we could not read: writing back would
    // save an empty list over the user's unreadable presets.
    if (parseFailed) return;

    // A preset is addressed BY NAME by everything downstream — recipes snapshot
    // the vessel/pitcher by name and re-select it on activation, and the recipe
    // wizard matches its tiles on it. Two presets sharing a name therefore both
    // light up in the wizard and activation resolves to whichever comes first,
    // so the clash is refused at the setter, where every caller inherits it.
    // Existing duplicates already in storage are left alone: this rejects new
    // ones, it does not delete anyone's data.
    if (nameTakenIn(arr, name, index)) {
        qWarning() << "SettingsBrew: refusing a duplicate water vessel named" << name
                   << "- that name is already in use";
        return;
    }

    if (index >= 0 && index < arr.size()) {
        QJsonObject preset;
        preset["name"] = name;
        preset["volume"] = volume;
        preset["mode"] = mode;
        preset["flowRate"] = flowRate;
        preset["temperature"] = temperature;
        arr[index] = preset;

        m_settings.setValue("water/vesselPresets", QJsonDocument(arr).toJson());
        emit waterVesselPresetsChanged();
    }
}

void SettingsBrew::removeWaterVesselPreset(int index) {
    bool parseFailed = false;
    QJsonArray arr = readPresetArray(QStringLiteral("water/vesselPresets"), &parseFailed);
    // Refuse to append to an array we could not read: writing back would
    // save an empty list over the user's unreadable presets.
    if (parseFailed) return;

    if (index >= 0 && index < arr.size()) {
        arr.removeAt(index);
        m_settings.setValue("water/vesselPresets", QJsonDocument(arr).toJson());

        int selected = selectedWaterVessel();
        if (selected >= arr.size() && arr.size() > 0) {
            setSelectedWaterCup(static_cast<int>(arr.size()) - 1);
        }

        emit waterVesselPresetsChanged();
    }
}

void SettingsBrew::moveWaterVesselPreset(int from, int to) {
    bool parseFailed = false;
    QJsonArray arr = readPresetArray(QStringLiteral("water/vesselPresets"), &parseFailed);
    // Refuse to append to an array we could not read: writing back would
    // save an empty list over the user's unreadable presets.
    if (parseFailed) return;

    if (from >= 0 && from < arr.size() && to >= 0 && to < arr.size() && from != to) {
        QJsonValue item = arr[from];
        arr.removeAt(from);
        arr.insert(to, item);
        m_settings.setValue("water/vesselPresets", QJsonDocument(arr).toJson());

        int selected = selectedWaterVessel();
        if (selected == from) {
            setSelectedWaterCup(to);
        } else if (from < selected && to >= selected) {
            setSelectedWaterCup(selected - 1);
        } else if (from > selected && to <= selected) {
            setSelectedWaterCup(selected + 1);
        }

        emit waterVesselPresetsChanged();
    }
}

QVariantMap SettingsBrew::getWaterVesselPreset(int index) const {
    QJsonArray arr = readPresetArray(QStringLiteral("water/vesselPresets"));

    if (index >= 0 && index < arr.size()) {
        return arr[index].toObject().toVariantMap();
    }
    return QVariantMap();
}

// Flush

QVariantList SettingsBrew::flushPresets() const {
    QJsonArray arr = readPresetArray(QStringLiteral("flush/presets"));

    QVariantList result;
    for (const QJsonValue& v : arr) {
        result.append(v.toObject().toVariantMap());
    }
    return result;
}

int SettingsBrew::selectedFlushPreset() const {
    return m_settings.value("flush/selectedPreset", 0).toInt();
}

void SettingsBrew::setSelectedFlushPreset(int index) {
    if (selectedFlushPreset() != index) {
        m_settings.setValue("flush/selectedPreset", index);
        emit selectedFlushPresetChanged();
    }
}

double SettingsBrew::flushFlow() const {
    return m_settings.value("flush/flow", 6.0).toDouble();
}

void SettingsBrew::setFlushFlow(double flow) {
    if (flushFlow() != flow) {
        m_settings.setValue("flush/flow", flow);
        emit flushFlowChanged();
    }
}

double SettingsBrew::flushSeconds() const {
    return m_settings.value("flush/seconds", 5.0).toDouble();
}

void SettingsBrew::setFlushSeconds(double seconds) {
    if (flushSeconds() != seconds) {
        m_settings.setValue("flush/seconds", seconds);
        emit flushSecondsChanged();
    }
}

void SettingsBrew::addFlushPreset(const QString& name, double flow, double seconds) {
    bool parseFailed = false;
    QJsonArray arr = readPresetArray(QStringLiteral("flush/presets"), &parseFailed);
    // Refuse to append to an array we could not read: writing back would
    // save an empty list over the user's unreadable presets.
    if (parseFailed) return;

    QJsonObject preset;
    preset["name"] = name;
    preset["flow"] = flow;
    preset["seconds"] = seconds;
    arr.append(preset);

    m_settings.setValue("flush/presets", QJsonDocument(arr).toJson());
    emit flushPresetsChanged();
}

void SettingsBrew::updateFlushPreset(int index, const QString& name, double flow, double seconds) {
    bool parseFailed = false;
    QJsonArray arr = readPresetArray(QStringLiteral("flush/presets"), &parseFailed);
    // Refuse to append to an array we could not read: writing back would
    // save an empty list over the user's unreadable presets.
    if (parseFailed) return;

    if (index >= 0 && index < arr.size()) {
        QJsonObject preset;
        preset["name"] = name;
        preset["flow"] = flow;
        preset["seconds"] = seconds;
        arr[index] = preset;

        m_settings.setValue("flush/presets", QJsonDocument(arr).toJson());
        emit flushPresetsChanged();
    }
}

void SettingsBrew::removeFlushPreset(int index) {
    bool parseFailed = false;
    QJsonArray arr = readPresetArray(QStringLiteral("flush/presets"), &parseFailed);
    // Refuse to append to an array we could not read: writing back would
    // save an empty list over the user's unreadable presets.
    if (parseFailed) return;

    if (index >= 0 && index < arr.size()) {
        arr.removeAt(index);
        m_settings.setValue("flush/presets", QJsonDocument(arr).toJson());

        int selected = selectedFlushPreset();
        if (selected >= arr.size() && arr.size() > 0) {
            setSelectedFlushPreset(static_cast<int>(arr.size()) - 1);
        }

        emit flushPresetsChanged();
    }
}

void SettingsBrew::moveFlushPreset(int from, int to) {
    bool parseFailed = false;
    QJsonArray arr = readPresetArray(QStringLiteral("flush/presets"), &parseFailed);
    // Refuse to append to an array we could not read: writing back would
    // save an empty list over the user's unreadable presets.
    if (parseFailed) return;

    if (from >= 0 && from < arr.size() && to >= 0 && to < arr.size() && from != to) {
        QJsonValue item = arr[from];
        arr.removeAt(from);
        arr.insert(to, item);
        m_settings.setValue("flush/presets", QJsonDocument(arr).toJson());

        int selected = selectedFlushPreset();
        if (selected == from) {
            setSelectedFlushPreset(to);
        } else if (from < selected && to >= selected) {
            setSelectedFlushPreset(selected - 1);
        } else if (from > selected && to <= selected) {
            setSelectedFlushPreset(selected + 1);
        }

        emit flushPresetsChanged();
    }
}

QVariantMap SettingsBrew::getFlushPreset(int index) const {
    QJsonArray arr = readPresetArray(QStringLiteral("flush/presets"));

    if (index >= 0 && index < arr.size()) {
        return arr[index].toObject().toVariantMap();
    }
    return QVariantMap();
}

// Temperature override (persistent)

double SettingsBrew::temperatureOverride() const {
    return m_temperatureOverride;
}

void SettingsBrew::setTemperatureOverride(double temp) {
    if (!qFuzzyCompare(m_temperatureOverride, temp) || !m_hasTemperatureOverride) {
        qDebug() << "setTemperatureOverride:" << m_temperatureOverride << "→" << temp;
        m_temperatureOverride = temp;
        m_hasTemperatureOverride = true;
        m_settings.setValue("brew/temperatureOverride", temp);
        m_settings.setValue("brew/hasTemperatureOverride", true);
        emit temperatureOverrideChanged();
    }
}

bool SettingsBrew::hasTemperatureOverride() const {
    return m_hasTemperatureOverride;
}

void SettingsBrew::clearTemperatureOverride() {
    if (m_hasTemperatureOverride || !qFuzzyIsNull(m_temperatureOverride)) {
        m_hasTemperatureOverride = false;
        m_temperatureOverride = 0.0;
        m_settings.remove("brew/temperatureOverride");
        m_settings.remove("brew/hasTemperatureOverride");
        emit temperatureOverrideChanged();
    }
}

// Brew yield override (persistent) — the session yield anchor {value, mode}.

double SettingsBrew::brewYieldOverride() const {
    return m_brewYieldOverride;
}

QString SettingsBrew::brewYieldMode() const {
    return m_brewYieldMode;
}

void SettingsBrew::writeBrewYieldAnchor(double value, const QString& mode) {
    QString newMode = YieldSpec::normalizedMode(mode);
    double newValue = value;
    if (newValue <= 0)
        newMode = YieldSpec::modeNone();
    // clampValue bounds BOTH modes from the shared vocabulary — the absolute
    // bound used to be a magic qBound here, which is how the bag's writers
    // came to enforce no upper bound at all.
    newValue = YieldSpec::clampValue(newMode, newValue);

    const bool changed = newMode != m_brewYieldMode
        || !qFuzzyCompare(1.0 + m_brewYieldOverride, 1.0 + newValue);
    if (!changed)
        return;

    m_brewYieldOverride = newValue;
    m_brewYieldMode = newMode;
    if (YieldSpec::isSet(newMode)) {
        m_settings.setValue("brew/brewYieldOverride", newValue);
        m_settings.setValue("espresso/brewYieldMode", newMode);
        // Legacy flag kept in sync so a downgraded build still sees the
        // anchor as an active override.
        m_settings.setValue("brew/hasBrewYieldOverride", true);
    } else {
        m_settings.remove("brew/brewYieldOverride");
        m_settings.remove("espresso/brewYieldMode");
        m_settings.remove("brew/hasBrewYieldOverride");
    }
    emit brewOverridesChanged();
}

void SettingsBrew::setBrewYieldOverride(double yield) {
    writeBrewYieldAnchor(yield, YieldSpec::modeAbsolute());
}

void SettingsBrew::setBrewRatioAnchor(double ratio) {
    writeBrewYieldAnchor(ratio, YieldSpec::modeRatio());
}

void SettingsBrew::setBrewYieldAnchor(double value, const QString& mode) {
    writeBrewYieldAnchor(value, mode);
}

bool SettingsBrew::hasBrewYieldOverride() const {
    return YieldSpec::isSet(m_brewYieldMode);
}

void SettingsBrew::clearAllBrewOverrides() {
    bool changed = false;

    if (YieldSpec::isSet(m_brewYieldMode) || !qFuzzyIsNull(m_brewYieldOverride)) {
        m_brewYieldOverride = 0.0;
        m_brewYieldMode = YieldSpec::modeNone();
        m_settings.remove("brew/brewYieldOverride");
        m_settings.remove("espresso/brewYieldMode");
        m_settings.remove("brew/hasBrewYieldOverride");
        changed = true;
    }

    bool tempChanged = false;
    if (m_hasTemperatureOverride || !qFuzzyIsNull(m_temperatureOverride)) {
        m_temperatureOverride = 0.0;
        m_hasTemperatureOverride = false;
        m_settings.remove("brew/temperatureOverride");
        m_settings.remove("brew/hasTemperatureOverride");
        changed = true;
        tempChanged = true;
    }

    if (changed) {
        emit brewOverridesChanged();
    }
    if (tempChanged) {
        emit temperatureOverrideChanged();
    }
}

void SettingsBrew::clearProfileScopedBrewOverrides() {
    // A profile load clears what the outgoing profile owned: the temperature
    // override (always) and an ABSOLUTE yield anchor (36 g was about THAT
    // profile). A ratio anchor survives — a ratio is profile-independent, and
    // this asymmetry is what delivers "persistent brew-by-ratio" with no
    // setting (add-yield-ratio-anchor Decision 8).
    if (m_brewYieldMode == YieldSpec::modeRatio()) {
        clearTemperatureOverride();
        return;
    }
    clearAllBrewOverrides();
}

// Stop-at-volume gating

bool SettingsBrew::ignoreVolumeWithScale() const {
    return m_settings.value("espresso/ignoreVolumeWithScale", false).toBool();
}

void SettingsBrew::setIgnoreVolumeWithScale(bool enabled) {
    if (ignoreVolumeWithScale() != enabled) {
        m_settings.setValue("espresso/ignoreVolumeWithScale", enabled);
        emit ignoreVolumeWithScaleChanged();
    }
}

