#pragma once

#include <QObject>
#include "appsettings.h"
#include <QString>
#include <QVariantList>
#include <QVariantMap>

// Forward-declared rather than included: only the private preset-array helper
// below needs it, and settings.h now includes this header (see CLAUDE.md).
class QJsonArray;

// Brew-domain settings: espresso, steam, hot water, flush, presets, and
// session-only brew/temperature overrides. Split from Settings to keep
// settings.h's transitive-include footprint small.
class SettingsBrew : public QObject {
    Q_OBJECT

    // Espresso
    Q_PROPERTY(double espressoTemperature READ espressoTemperature WRITE setEspressoTemperature NOTIFY espressoTemperatureChanged FINAL)
    Q_PROPERTY(double targetWeight READ targetWeight WRITE setTargetWeight NOTIFY targetWeightChanged FINAL)
    Q_PROPERTY(double lastUsedRatio READ lastUsedRatio WRITE setLastUsedRatio NOTIFY lastUsedRatioChanged FINAL)
    // Home-screen quick-select brew-ratio presets (Ristretto / Normale / Lungo),
    // used by the ratioQuickSelect layout widget. Default 1 / 2 / 3.
    Q_PROPERTY(double ratioPreset1 READ ratioPreset1 WRITE setRatioPreset1 NOTIFY ratioPreset1Changed FINAL)
    Q_PROPERTY(double ratioPreset2 READ ratioPreset2 WRITE setRatioPreset2 NOTIFY ratioPreset2Changed FINAL)
    Q_PROPERTY(double ratioPreset3 READ ratioPreset3 WRITE setRatioPreset3 NOTIFY ratioPreset3Changed FINAL)
    // Dose cup tare: empty weight of the dosing vessel, subtracted from the scale
    // reading in "Get from scale" so the dose is net beans. Default 0 = no tare.
    Q_PROPERTY(double doseCupTareWeight READ doseCupTareWeight WRITE setDoseCupTareWeight NOTIFY doseCupTareWeightChanged FINAL)
    // Master toggle for weight-timed steaming (UI label "Weight-timed steaming").
    // When off, steam time is never scaled from milk weight. Default OFF; setting a
    // pitcher's reference milk (setSteamPitcherCalibration) turns it on automatically.
    Q_PROPERTY(bool milkAutoCaptureEnabled READ milkAutoCaptureEnabled WRITE setMilkAutoCaptureEnabled NOTIFY milkAutoCaptureEnabledChanged FINAL)
    // Whether the confirmation "ding" plays when a dose/milk auto-captures. Default
    // off — toggled by the bell on the Dose cup row.
    Q_PROPERTY(bool doseCaptureSoundEnabled READ doseCaptureSoundEnabled WRITE setDoseCaptureSoundEnabled NOTIFY doseCaptureSoundEnabledChanged FINAL)
    // Last actual steam session (milk weight + steam time), saved so the steam
    // setup can adopt them as a new reference baseline.
    Q_PROPERTY(double lastSteamMilkG READ lastSteamMilkG WRITE setLastSteamMilkG NOTIFY lastSteamMilkGChanged FINAL)
    Q_PROPERTY(double lastSteamTimeS READ lastSteamTimeS WRITE setLastSteamTimeS NOTIFY lastSteamTimeSChanged FINAL)
    // Global weight-timed steam rate (seconds of steam per gram of milk). One
    // calibration for every pitcher (assumes a consistent steam flow — a
    // simplification, not a physical guarantee), replacing per-pitcher
    // reference-milk scaling. 0 = uncalibrated. Clamped >= 0.
    Q_PROPERTY(double steamSecondsPerGram READ steamSecondsPerGram WRITE setSteamSecondsPerGram NOTIFY steamSecondsPerGramChanged FINAL)

    // Steam
    Q_PROPERTY(double steamTemperature READ steamTemperature WRITE setSteamTemperature NOTIFY steamTemperatureChanged FINAL)
    Q_PROPERTY(int steamTimeout READ steamTimeout WRITE setSteamTimeout NOTIFY steamTimeoutChanged FINAL)
    Q_PROPERTY(int steamFlow READ steamFlow WRITE setSteamFlow NOTIFY steamFlowChanged FINAL)
    // Session-only flag (no QSettings backing) — used during descaling to suppress
    // the steam heater. Setter is Q_INVOKABLE rather than a property WRITE so the
    // public API doesn't pretend this value persists across restarts.
    Q_PROPERTY(bool steamDisabled READ steamDisabled NOTIFY steamDisabledChanged FINAL)
    Q_PROPERTY(bool keepSteamHeaterOn READ keepSteamHeaterOn WRITE setKeepSteamHeaterOn NOTIFY keepSteamHeaterOnChanged FINAL)
    Q_PROPERTY(int steamAutoFlushSeconds READ steamAutoFlushSeconds WRITE setSteamAutoFlushSeconds NOTIFY steamAutoFlushSecondsChanged FINAL)

    // Steam pitcher presets
    Q_PROPERTY(QVariantList steamPitcherPresets READ steamPitcherPresets NOTIFY steamPitcherPresetsChanged FINAL)
    Q_PROPERTY(int selectedSteamPitcher READ selectedSteamPitcher WRITE setSelectedSteamCup NOTIFY selectedSteamPitcherChanged FINAL)

    // Hot water
    Q_PROPERTY(double waterTemperature READ waterTemperature WRITE setWaterTemperature NOTIFY waterTemperatureChanged FINAL)
    Q_PROPERTY(int waterVolume READ waterVolume WRITE setWaterVolume NOTIFY waterVolumeChanged FINAL)
    Q_PROPERTY(QString waterVolumeMode READ waterVolumeMode WRITE setWaterVolumeMode NOTIFY waterVolumeModeChanged FINAL)
    Q_PROPERTY(double hotWaterSawOffset READ hotWaterSawOffset WRITE setHotWaterSawOffset NOTIFY hotWaterSawOffsetChanged FINAL)
    Q_PROPERTY(int hotWaterSawSampleCount READ hotWaterSawSampleCount WRITE setHotWaterSawSampleCount NOTIFY hotWaterSawSampleCountChanged FINAL)

    // Hot water vessel presets
    Q_PROPERTY(QVariantList waterVesselPresets READ waterVesselPresets NOTIFY waterVesselPresetsChanged FINAL)
    Q_PROPERTY(int selectedWaterVessel READ selectedWaterVessel WRITE setSelectedWaterCup NOTIFY selectedWaterVesselChanged FINAL)

    // Flush presets
    Q_PROPERTY(QVariantList flushPresets READ flushPresets NOTIFY flushPresetsChanged FINAL)
    Q_PROPERTY(int selectedFlushPreset READ selectedFlushPreset WRITE setSelectedFlushPreset NOTIFY selectedFlushPresetChanged FINAL)
    Q_PROPERTY(double flushFlow READ flushFlow WRITE setFlushFlow NOTIFY flushFlowChanged FINAL)
    Q_PROPERTY(double flushSeconds READ flushSeconds WRITE setFlushSeconds NOTIFY flushSecondsChanged FINAL)

    // Temperature override (persistent)
    Q_PROPERTY(double temperatureOverride READ temperatureOverride WRITE setTemperatureOverride NOTIFY temperatureOverrideChanged FINAL)
    Q_PROPERTY(bool hasTemperatureOverride READ hasTemperatureOverride NOTIFY temperatureOverrideChanged FINAL)

    // Brew parameter overrides (persistent). The yield override is the SESSION
    // YIELD ANCHOR (add-yield-ratio-anchor): {value, mode} where mode is
    // "none" | "absolute" | "ratio" (src/core/yieldspec.h). brewYieldOverride
    // is the anchor VALUE in its mode's own unit — grams when absolute, a
    // dose multiplier when ratio. Writing through the legacy property setter
    // anchors an ABSOLUTE (grams); ratio writers use setBrewRatioAnchor().
    // hasBrewYieldOverride is defined as mode != none — never inferred by
    // comparing a resolved gram value against the profile target.
    Q_PROPERTY(double brewYieldOverride READ brewYieldOverride WRITE setBrewYieldOverride NOTIFY brewOverridesChanged FINAL)
    Q_PROPERTY(QString brewYieldMode READ brewYieldMode NOTIFY brewOverridesChanged FINAL)
    Q_PROPERTY(bool hasBrewYieldOverride READ hasBrewYieldOverride NOTIFY brewOverridesChanged FINAL)

    // Stop-at-volume gating when a BLE scale provides weight data
    Q_PROPERTY(bool ignoreVolumeWithScale READ ignoreVolumeWithScale WRITE setIgnoreVolumeWithScale NOTIFY ignoreVolumeWithScaleChanged FINAL)

public:
    explicit SettingsBrew(QObject* parent = nullptr);

    // Espresso
    double espressoTemperature() const;
    void setEspressoTemperature(double temp);

    double targetWeight() const;
    void setTargetWeight(double weight);

    double lastUsedRatio() const;
    void setLastUsedRatio(double ratio);

    double ratioPreset1() const;
    void setRatioPreset1(double r);
    double ratioPreset2() const;
    void setRatioPreset2(double r);
    double ratioPreset3() const;
    void setRatioPreset3(double r);

    double doseCupTareWeight() const;
    void setDoseCupTareWeight(double weight);

    bool milkAutoCaptureEnabled() const;
    void setMilkAutoCaptureEnabled(bool enabled);
    bool doseCaptureSoundEnabled() const;
    void setDoseCaptureSoundEnabled(bool enabled);

    double lastSteamMilkG() const;
    void setLastSteamMilkG(double g);
    double lastSteamTimeS() const;
    void setLastSteamTimeS(double s);

    double steamSecondsPerGram() const;
    void setSteamSecondsPerGram(double secPerGram);
    // Calibrate the global steam rate from one observed steam: secPerGram =
    // timeSec / milkG. Guarded (both > 0). Turns weight-timed steaming on, matching
    // the old per-pitcher calibrate opt-in.
    Q_INVOKABLE void calibrateSteamFromReference(double milkG, double timeSec);
    // Derive a global rate from the legacy per-pitcher (calibMilkG, duration) — first
    // calibrated preset wins, 0 if none. Shared by the ctor migration and the backup-
    // import re-seed. seed…() applies it (positive only) through the setter.
    double deriveSteamRateFromLegacyPresets() const;
    void seedSteamRateFromLegacyPresets();

    // Steam
    double steamTemperature() const;
    void setSteamTemperature(double temp);

    int steamTimeout() const;
    void setSteamTimeout(int timeout);

    int steamFlow() const;
    void setSteamFlow(int flow);

    bool steamDisabled() const;
    Q_INVOKABLE void setSteamDisabled(bool disabled);

    bool keepSteamHeaterOn() const;
    void setKeepSteamHeaterOn(bool keep);

    int steamAutoFlushSeconds() const;
    void setSteamAutoFlushSeconds(int seconds);

    // Steam pitcher presets
    QVariantList steamPitcherPresets() const;
    int selectedSteamPitcher() const;
    void setSelectedSteamCup(int index);

    Q_INVOKABLE void addSteamPitcherPreset(const QString& name, int duration, int flow, double temperature = 160.0);
    Q_INVOKABLE void addSteamPitcherPresetDisabled(const QString& name);
    Q_INVOKABLE void updateSteamPitcherPreset(int index, const QString& name, int duration, int flow, double temperature = 160.0);
    Q_INVOKABLE void removeSteamPitcherPreset(int index);
    Q_INVOKABLE void moveSteamPitcherPreset(int from, int to);
    Q_INVOKABLE QVariantMap getSteamPitcherPreset(int index) const;
    // As waterVesselNameTaken: for a dialog to check before it calls a setter.
    Q_INVOKABLE bool steamPitcherNameTaken(const QString& name, int ignoreIndex = -1) const;
    Q_INVOKABLE void setSteamPitcherWeight(int index, double weightG);
    // Weight-scaled steaming: pair a reference milk weight with this preset's
    // duration. At steam time the duration is scaled by the actual milk weight.
    Q_INVOKABLE void setSteamPitcherCalibration(int index, double calibMilkG);
    // Net milk on the scale for a pitcher = scaleReading − saved empty-pitcher weight.
    // Returns 0 if no empty-pitcher weight is saved (one consistent net-milk rule) or
    // the result is outside the plausible milk range. Pure measurement — not gated by
    // the weight-timing toggle.
    Q_INVOKABLE double netMilkForPitcher(int index, double scaleReading) const;
    // Single source of truth for weight-timed steam scaling. Returns the scaled steam
    // time (s) clamped to [5,120], or 0 — meaning "use the preset's fixed duration" —
    // when weight-timing is off, or the preset is missing/disabled/uncalibrated, or
    // the milk weight / duration is non-positive.
    Q_INVOKABLE int scaledSteamTime(int index, double milkG) const;
    // Scaled-or-base resolution in one place: scaledSteamTime() when it yields a scaled
    // value, else the preset's fixed duration (0 for a missing/disabled preset; the
    // missing case also warns). Used by every preset-selection timeout write (idle
    // pills, SteamPage pitcher cards + live pills, Steam widget popup, MCP pitcher
    // select), SteamPage's syncSteamTimeout, and the steam-plan display so they all
    // agree. Steam-start and milk auto-capture writes deliberately use
    // scaledSteamTime() only — they must never replace a manually-adjusted or
    // already-scaled timeout with the base duration.
    Q_INVOKABLE int effectiveSteamDurationSec(int index, double milkG) const;

    // Hot water
    double waterTemperature() const;
    void setWaterTemperature(double temp);

    int waterVolume() const;
    void setWaterVolume(int volume);

    QString waterVolumeMode() const;  // "weight" or "volume"
    void setWaterVolumeMode(const QString& mode);

    // Hot water volume byte to send in DE1 ShotSettings. In "weight" mode the
    // app stops hot water via the scale and sends 0 so the DE1 flowmeter
    // auto-stop is disabled; in "volume" mode it returns the clamped
    // waterVolume(). All setShotSettings() call sites must use this to keep
    // payloads consistent — otherwise the ShotSettings drift detector trips
    // on BLE echo reordering.
    int effectiveHotWaterVolume() const;

    double hotWaterSawOffset() const;
    void setHotWaterSawOffset(double offset);
    int hotWaterSawSampleCount() const;
    void setHotWaterSawSampleCount(int count);

    // Hot water vessel presets
    QVariantList waterVesselPresets() const;
    int selectedWaterVessel() const;
    void setSelectedWaterCup(int index);

    Q_INVOKABLE void addWaterVesselPreset(const QString& name, int volume, const QString& mode = "weight", int flowRate = 40, double temperature = 85.0);
    Q_INVOKABLE void updateWaterVesselPreset(int index, const QString& name, int volume, const QString& mode = "weight", int flowRate = 40, double temperature = 85.0);
    Q_INVOKABLE void removeWaterVesselPreset(int index);
    Q_INVOKABLE void moveWaterVesselPreset(int from, int to);
    Q_INVOKABLE QVariantMap getWaterVesselPreset(int index) const;
    // Lets a dialog block Save and explain itself BEFORE calling the setters
    // above, which reject a clash silently (they return void). ignoreIndex is
    // the preset being renamed; pass -1 when adding.
    Q_INVOKABLE bool waterVesselNameTaken(const QString& name, int ignoreIndex = -1) const;

    // Flush
    QVariantList flushPresets() const;
    int selectedFlushPreset() const;
    void setSelectedFlushPreset(int index);

    double flushFlow() const;
    void setFlushFlow(double flow);

    double flushSeconds() const;
    void setFlushSeconds(double seconds);

    Q_INVOKABLE void addFlushPreset(const QString& name, double flow, double seconds);
    Q_INVOKABLE void updateFlushPreset(int index, const QString& name, double flow, double seconds);
    Q_INVOKABLE void removeFlushPreset(int index);
    Q_INVOKABLE void moveFlushPreset(int from, int to);
    Q_INVOKABLE QVariantMap getFlushPreset(int index) const;

    // Temperature override (persistent)
    double temperatureOverride() const;
    void setTemperatureOverride(double temp);
    bool hasTemperatureOverride() const;
    Q_INVOKABLE void clearTemperatureOverride();

    // Brew parameter overrides (persistent) — the session yield anchor.
    double brewYieldOverride() const;   // anchor value, in its mode's unit
    QString brewYieldMode() const;      // "none" | "absolute" | "ratio"
    // Anchor an absolute gram target (mode -> "absolute"); <= 0 clears the
    // anchor entirely (mode -> "none"). Grams clamp to [1, 500].
    void setBrewYieldOverride(double yield);
    // Anchor a ratio (mode -> "ratio"); <= 0 clears. Clamped to the single
    // C++ ratio bound (YieldSpec::clampRatio, 0.5–6.0) that every ratio
    // write boundary shares.
    Q_INVOKABLE void setBrewRatioAnchor(double ratio);
    // Restore a stored spec verbatim (recipe/bag activation): mode is
    // normalized, values clamped per mode; mode "none" clears.
    void setBrewYieldAnchor(double value, const QString& mode);
    bool hasBrewYieldOverride() const;  // == mode != "none"
    Q_INVOKABLE void clearAllBrewOverrides();
    // The profile-load reset (add-yield-ratio-anchor): clears the temperature
    // override unconditionally and an ABSOLUTE yield anchor (a gram target
    // describes the profile it was set against), but KEEPS a ratio anchor —
    // 1:2 is 1:2 on any profile. Callers that must drop everything (explicit
    // user Clear, profile edit, new profile) use clearAllBrewOverrides().
    Q_INVOKABLE void clearProfileScopedBrewOverrides();

    // Stop-at-volume gating
    bool ignoreVolumeWithScale() const;
    void setIgnoreVolumeWithScale(bool enabled);

signals:
    void espressoTemperatureChanged();
    void targetWeightChanged();
    void lastUsedRatioChanged();
    void ratioPreset1Changed();
    void ratioPreset2Changed();
    void ratioPreset3Changed();
    void doseCupTareWeightChanged();
    void milkAutoCaptureEnabledChanged();
    void doseCaptureSoundEnabledChanged();
    void lastSteamMilkGChanged();
    void lastSteamTimeSChanged();
    void steamSecondsPerGramChanged();
    void steamTemperatureChanged();
    void steamTimeoutChanged();
    void steamFlowChanged();
    void steamDisabledChanged();
    void keepSteamHeaterOnChanged();
    void steamAutoFlushSecondsChanged();
    void steamPitcherPresetsChanged();
    void selectedSteamPitcherChanged();
    void waterTemperatureChanged();
    void waterVolumeChanged();
    void waterVolumeModeChanged();
    void hotWaterSawOffsetChanged();
    void hotWaterSawSampleCountChanged();
    void waterVesselPresetsChanged();
    void selectedWaterVesselChanged();
    void flushPresetsChanged();
    void selectedFlushPresetChanged();
    void flushFlowChanged();
    void flushSecondsChanged();
    void temperatureOverrideChanged();
    void brewOverridesChanged();
    void ignoreVolumeWithScaleChanged();

private:
    mutable AppSettings m_settings;

    // Session-only steam-disable flag (used during descaling)
    bool m_steamDisabled = false;

    // Persistent overrides — backing fields cached so getters don't hit QSettings.
    double m_temperatureOverride = 0.0;
    bool m_hasTemperatureOverride = false;
    double m_brewYieldOverride = 0.0;
    QString m_brewYieldMode;  // normalized; "none" when no anchor is armed

    // Shared write path for the session anchor: normalizes + clamps, updates
    // the cache and QSettings, emits brewOverridesChanged once.
    void writeBrewYieldAnchor(double value, const QString& mode);

    // Every preset list (steam pitchers, water vessels, flushes) is stored as a
    // JSON array in one settings key and read back through here.
    //
    // The point of the helper is the `parseFailed` out-parameter. Reading with
    // the no-error overload of QJsonDocument::fromJson makes unreadable bytes
    // indistinguishable from an absent key — both yield an empty array — so a
    // reader reports "you have no presets" and the next writer appends to that
    // empty array and SAVES IT OVER the bytes it could not read. The user is
    // told their presets are gone and then handed the action that makes it
    // true. Mutators therefore check the flag and refuse to write.
    //
    // Pass nullptr when a caller genuinely cannot act on the distinction; the
    // warning is still logged.
    QJsonArray readPresetArray(const QString& key, bool* parseFailed = nullptr) const;

    // Case-insensitive, whitespace-trimmed name search. ignoreIndex is the entry
    // being renamed (so renaming a preset to its own name is not a clash); pass
    // -1 when adding.
    static bool nameTakenIn(const QJsonArray& arr, const QString& name, int ignoreIndex);
};
