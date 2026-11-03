#pragma once

#include "recipewaterstate.h"
#include "drinksequencecontroller.h"
#include "core/machinecontrolpresets.h"
#include "core/settings.h"
#include "machine/machinestate.h"
#include "ble/de1device.h"
#include <QObject>
#include <QScopedValueRollback>

// Production event/settings adapter, shared by MainController and integration tests.
class RecipeWaterController final : public QObject {
public:
    RecipeWaterController(Settings* settings, DE1Device* device, MachineState* machine,
                          DrinkSequenceController* sequence, std::function<void()> send, QObject* parent,
                          std::function<void()> differentVesselSelected = {})
        : QObject(parent), m_settings(settings), m_machine(machine), m_sequence(sequence), m_send(std::move(send)),
          m_differentVesselSelected(std::move(differentVesselSelected)) {
        auto* brew = settings->brew();
        const auto edited = [this] { standaloneSettingsEdited(); };
        connect(brew, &SettingsBrew::waterVolumeChanged, this, edited);
        connect(brew, &SettingsBrew::waterVolumeModeChanged, this, edited);
        connect(brew, &SettingsBrew::waterTemperatureChanged, this, edited);
        connect(settings->hardware(), &SettingsHardware::hotWaterFlowRateChanged, this, edited);
        connect(machine, &MachineState::espressoCycleStarted, this, [this] { m_state.beginDrink(); });
        connect(machine, &MachineState::espressoCycleEnded, this, [this] {
            if (!m_sequence->isActive()) finish();
        });
        connect(machine, &MachineState::shotFlowStarted, this, [this](int phase) {
            if (phase == static_cast<int>(MachineState::Phase::HotWater))
                m_state.waterStarted(m_machine->hotWaterCycleId(), livePlan());
        });
        connect(machine, &MachineState::phaseChanged, this, [this] {
            if (m_machine->phase() == MachineState::Phase::Disconnected
                || m_machine->phase() == MachineState::Phase::Sleep) {
                finish();
            } else if (m_state.waterCaptured() && m_machine->phase() != MachineState::Phase::HotWater
                       && !m_sequence->isActive()) {
                finish();
            }
        });
        connect(sequence, &DrinkSequenceController::activeChanged, this, [this] {
            if (!m_sequence->isActive() && m_machine->phase() != MachineState::Phase::HotWater) finish();
        });
        connect(device, &DE1Device::connectedChanged, this, [this, device] {
            if (!device->isConnected()) finish();
        });
    }

    void recipeSetup(qint64 id, QJsonObject water, double targetG, bool targetEdited) {
        if (id <= 0 && m_state.setupRecipeId() <= 0) return;
        water = normalize(water);
        m_state.setup(id, water, targetG, targetEdited);
        apply();
    }
    void bindEspresso(qint64 operation) { m_state.bindEspresso(operation); }
    void espressoTargetEdited(qint64 operation, double targetG) {
        m_state.espressoTargetEdited(operation, targetG);
        apply();
    }
    bool selectVessel(int index) {
        const auto preset = m_settings->brew()->getWaterVesselPreset(index);
        if (preset.isEmpty()) return false;
        const auto& setup = m_state.setupPlan();
        if (m_state.setupRecipeId() > 0 && setup.enabled()
            && preset.value(QStringLiteral("name")).toString().compare(
                setup.spec.snapshot.value(QStringLiteral("vesselName")).toString(), Qt::CaseInsensitive) == 0) {
            apply();
            return true;
        }
        if (m_state.setupRecipeId() > 0 && setup.enabled() && m_differentVesselSelected)
            m_differentVesselSelected();
        m_settings->brew()->setSelectedWaterCup(index);
        auto water = QJsonObject::fromVariantMap(preset);
        water.insert(QStringLiteral("hasWater"), true);
        water.insert(QStringLiteral("vesselName"), preset.value(QStringLiteral("name")).toString());
        water.insert(QStringLiteral("temperatureC"), preset.value(QStringLiteral("temperature")).toDouble());
        m_state.setup(0, water, 0);
        apply();
        return true;
    }
    QVariantMap waterResult(qint64 operation, QVariantMap result) const {
        const auto& plan = m_state.effectivePlan();
        if (result.value(QStringLiteral("status")).toString() == QLatin1String("notDispensed")
            && m_state.drinkOperation() == operation && plan.spec.ratio() && plan.valid())
            result.insert(QStringLiteral("targetWeightG"), plan.result.amount);
        return result;
    }
    void presetEdited() {
        if (m_state.setupRecipeId() > 0 && m_state.setupPlan().enabled()) apply();
        else (void)selectVessel(m_settings->brew()->selectedWaterVessel());
    }
    QString startError(bool activationPending) const { return m_state.startError(activationPending); }
    QJsonObject historySnapshot() const { return m_state.historySnapshot(); }
    const RecipeWaterState& state() const { return m_state; }

    void apply() {
        if (m_applying) return;
        const auto& plan = m_state.effectivePlan();
        if (!plan.enabled() || !plan.valid()) return;
        const QScopedValueRollback<bool> applying(m_applying, true);
        m_state.applicationResult(MachineControlPresets::applyWaterPlan(
            m_settings->brew(), m_settings->hardware(), plan, m_send));
    }

private:
    void standaloneSettingsEdited() {
        if (m_applying || (m_state.setupRecipeId() > 0 && m_state.setupPlan().enabled())) return;
        const auto plan = livePlan();
        if (plan.spec.snapshot.value(QStringLiteral("vesselName")).toString().isEmpty()) return;
        m_state.setup(0, plan.spec.snapshot, 0);
    }
    QJsonObject normalize(QJsonObject water) const {
        water = WaterAmount::writableSnapshot(water);
        const auto name = water.value(QStringLiteral("vesselName")).toString();
        QJsonObject preset;
        const auto presets = m_settings->brew()->waterVesselPresets();
        for (const auto& value : presets) {
            const auto row = value.toMap();
            if (row.value(QStringLiteral("name")).toString().compare(name, Qt::CaseInsensitive) == 0) {
                preset = QJsonObject::fromVariantMap(row);
                break;
            }
        }
        water = WaterAmount::withPresetFallback(water, preset);
        if (water.value(QStringLiteral("hasWater")).toBool()) {
            if (!water.contains(QStringLiteral("temperatureC")))
                water.insert(QStringLiteral("temperatureC"), m_settings->brew()->waterTemperature());
            if (!water.contains(QStringLiteral("flowRate"))) water.insert(QStringLiteral("flowRate"), 40);
        }
        return water;
    }
    WaterAmount::Plan livePlan() const {
        auto water = QJsonObject::fromVariantMap(m_settings->brew()->getWaterVesselPreset(
            m_settings->brew()->selectedWaterVessel()));
        water.insert(QStringLiteral("hasWater"), true);
        water.insert(QStringLiteral("vesselName"), water.value(QStringLiteral("name")));
        water.insert(QStringLiteral("volume"), m_settings->brew()->waterVolume());
        water.insert(QStringLiteral("mode"), m_settings->brew()->waterVolumeMode());
        water.insert(QStringLiteral("temperatureC"), m_settings->brew()->waterTemperature());
        water.insert(QStringLiteral("flowRate"), m_settings->hardware()->hotWaterFlowRate());
        return WaterAmount::plan(water, 0);
    }
    void finish() { m_state.endDrink(); apply(); }

    Settings* m_settings;
    MachineState* m_machine;
    DrinkSequenceController* m_sequence;
    std::function<void()> m_send;
    std::function<void()> m_differentVesselSelected;
    RecipeWaterState m_state;
    bool m_applying = false;
};
