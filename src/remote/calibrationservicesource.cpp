#include "calibrationservicesource.h"

#include "../ble/de1device.h"
#include "../controllers/profilemanager.h"
#include "../core/settings.h"
#include "../core/settings_brew.h"
#include "../core/settings_calibration.h"
#include "../core/settings_dye.h"
#include "../core/settings_hardware.h"
#include "../machine/machinestate.h"
#include "../machine/steamhealthtracker.h"
#include "calibrationoperationpolicy.h"

#include <QMetaType>
#include <QScopedValueRollback>

#include <cmath>
#include <limits>

namespace { constexpr qint64 kMaxSafeRevision = 9007199254740991LL; }
namespace {
bool integerValue(const QVariant& value, int* output)
{
    if (value.metaType().id() == QMetaType::Bool || value.metaType().id() == QMetaType::QString)
        return false;
    bool ok = false;
    const double number = value.toDouble(&ok);
    if (!ok || !std::isfinite(number) || std::trunc(number) != number
        || number < std::numeric_limits<int>::min() || number > std::numeric_limits<int>::max())
        return false;
    *output = static_cast<int>(number);
    return true;
}
}

NativeCalibrationSource::NativeCalibrationSource(Settings* settings, ProfileManager* profiles,
                                                 MachineState* machineState, DE1Device* device,
                                                 SteamHealthTracker* steamHealth, QObject* parent)
    : CalibrationStateSimpleSource(parent), m_settings(settings), m_profiles(profiles),
      m_machineState(machineState), m_device(device), m_steamHealth(steamHealth)
{
    if (m_settings) {
        connect(m_settings, &Settings::useFlowScaleChanged, this, [this] { hostChanged(); });
        connect(m_settings->brew(), &SettingsBrew::ignoreVolumeWithScaleChanged,
                this, [this] { hostChanged(); });
        connect(m_settings->calibration(), &SettingsCalibration::autoFlowCalibrationChanged,
                this, [this] { hostChanged(); });
        connect(m_settings->calibration(), &SettingsCalibration::flowCalibrationMultiplierChanged,
                this, [this] { hostChanged(); });
        connect(m_settings->calibration(), &SettingsCalibration::perProfileFlowCalibrationChanged,
                this, [this] { hostChanged(); });
        connect(m_settings->calibration(), &SettingsCalibration::sawLearnedLagChanged,
                this, [this] { hostChanged(); });
        connect(m_settings->hardware(), &SettingsHardware::heaterIdleTempChanged,
                this, [this] { hostChanged(); });
        connect(m_settings->hardware(), &SettingsHardware::heaterWarmupFlowChanged,
                this, [this] { hostChanged(); });
        connect(m_settings->hardware(), &SettingsHardware::heaterTestFlowChanged,
                this, [this] { hostChanged(); });
        connect(m_settings->hardware(), &SettingsHardware::heaterWarmupTimeoutChanged,
                this, [this] { hostChanged(); });
        connect(m_settings->hardware(), &SettingsHardware::fanThresholdChanged,
                this, [this] { hostChanged(); });
        connect(m_settings->hardware(), &SettingsHardware::steamTwoTapStopChanged,
                this, [this] { hostChanged(); });
        connect(m_settings->dye(), &SettingsDye::dyeBasketChanged,
                this, [this] { hostChanged(); });
    }
    if (m_profiles)
        connect(m_profiles, &ProfileManager::currentProfileChanged, this, [this] { hostChanged(); });
    if (m_machineState)
        connect(m_machineState, &MachineState::activeScaleTypeChanged, this, [this] { hostChanged(); });
    if (m_device) {
        connect(m_device, &DE1Device::connectedChanged, this, [this] { hostChanged(); });
        connect(m_device, &DE1Device::heaterVoltageChanged, this, [this] { hostChanged(); });
    }
    if (m_steamHealth)
        connect(m_steamHealth, &SteamHealthTracker::sessionHistoryChanged,
                this, [this] { hostChanged(); });
    publish();
}

void NativeCalibrationSource::refresh() { publish(); }

QVariantMap NativeCalibrationSource::capture() const
{
    if (!m_settings || !m_profiles || !m_machineState || !m_device || !m_steamHealth)
        return {};
    const QString profile = m_profiles->baseProfileName();
    const QString scale = m_machineState->activeScaleType();
    const auto* cal = m_settings->calibration();
    const auto* hardware = m_settings->hardware();
    const auto* health = m_steamHealth.data();
    return {
        {QStringLiteral("revision"), m_revision},
        {QStringLiteral("profileFilename"), profile},
        {QStringLiteral("profileTitle"), m_profiles->currentProfileTitle()},
        {QStringLiteral("activeScaleType"), scale},
        {QStringLiteral("activeScaleName"), m_machineState->activeScaleName()},
        {QStringLiteral("basketBrand"), m_settings->dye()->dyeBasketBrand()},
        {QStringLiteral("basketModel"), m_settings->dye()->dyeBasketModel()},
        {QStringLiteral("autoFlowCalibration"), cal->autoFlowCalibration()},
        {QStringLiteral("perProfileFlowCalVersion"), cal->perProfileFlowCalVersion()},
        {QStringLiteral("effectiveFlowCalibration"), cal->effectiveFlowCalibration(profile)},
        {QStringLiteral("hasProfileFlowCalibration"), cal->hasProfileFlowCalibration(profile)},
        {QStringLiteral("useFlowScale"), m_settings->useFlowScale()},
        {QStringLiteral("ignoreVolumeWithScale"), m_settings->brew()->ignoreVolumeWithScale()},
        {QStringLiteral("sawLearnedLag"), cal->sawLearnedLagFor(profile, scale)},
        {QStringLiteral("sawModelSource"), cal->sawModelSource(profile, scale)},
        {QStringLiteral("hasSawLearningForProfile"), cal->hasSawLearningForProfile(profile, scale)},
        {QStringLiteral("heaterIdleTemp"), hardware->heaterIdleTemp()},
        {QStringLiteral("heaterWarmupFlow"), hardware->heaterWarmupFlow()},
        {QStringLiteral("heaterTestFlow"), hardware->heaterTestFlow()},
        {QStringLiteral("heaterWarmupTimeout"), hardware->heaterWarmupTimeout()},
        {QStringLiteral("fanThreshold"), hardware->fanThreshold()},
        {QStringLiteral("steamTwoTapStop"), hardware->steamTwoTapStop()},
        {QStringLiteral("connected"), m_device->isConnected()},
        {QStringLiteral("nominalHeaterVoltage"), m_device->nominalHeaterVoltage()},
        {QStringLiteral("steamSessionCount"), health->sessionCount()},
        {QStringLiteral("steamHasData"), health->hasData()},
        {QStringLiteral("steamBaselinePressure"), health->baselinePressure()},
        {QStringLiteral("steamBaselineTemperature"), health->baselineTemperature()},
        {QStringLiteral("steamCurrentPressure"), health->currentPressure()},
        {QStringLiteral("steamCurrentTemperature"), health->currentTemperature()},
        {QStringLiteral("steamPressureThreshold"), health->pressureThreshold()},
        {QStringLiteral("steamTemperatureThreshold"), health->temperatureThreshold()},
        {QStringLiteral("steamMinSessionsForTrend"), health->minSessionsForTrend()},
        {QStringLiteral("steamBaselineState"), static_cast<int>(health->baselineState())}
    };
}

void NativeCalibrationSource::publish(bool changed)
{
    if (changed && m_revision < kMaxSafeRevision) ++m_revision;
    const QVariantMap state = capture();
    setSnapshot(state);
    setReady(!state.isEmpty());
    setRevision(m_revision);
    setErrorMessage(state.isEmpty() ? QStringLiteral("Calibration settings are unavailable") : QString());
}

void NativeCalibrationSource::hostChanged()
{
    if (m_applyingOperation) { m_hostChangedDuringOperation = true; return; }
    publish(true);
}

void NativeCalibrationSource::finish(const QString& id, const QString& operation, bool success,
                                     const QVariantMap& result, const QString& reason)
{
    if (!m_requests.finish(id)) return;
    emit operationCompleted(id, operation, success, result, reason, m_revision);
}

void NativeCalibrationSource::execute(QString id, QString operation, QVariantMap args,
                                      qint64 expectedRevision)
{
    const auto start = m_requests.begin(id);
    if (start == ServiceRequestIds::Start::DuplicateId) return;
    if (start != ServiceRequestIds::Start::Accepted) {
        emit operationCompleted(id, operation, false, {},
            start == ServiceRequestIds::Start::LimitReached ? QStringLiteral("tooManyRequests")
                                                            : QStringLiteral("invalidRequestId"), m_revision);
        return;
    }
    if (!m_settings || !m_profiles || !m_machineState || !m_device || !m_steamHealth) {
        finish(id, operation, false, {}, QStringLiteral("unavailable")); return;
    }
    if (!CalibrationOperationPolicy::supported(operation)) {
        finish(id, operation, false, {}, QStringLiteral("unsupportedOperation")); return;
    }
    if (expectedRevision != m_revision) { finish(id, operation, false, {}, QStringLiteral("conflict")); return; }
    QScopedValueRollback<bool> applyingGuard(m_applyingOperation, true);
    m_hostChangedDuringOperation = false;
    bool ok = false;
    if (operation == QLatin1String("setAutoFlowCalibration")) {
        const QVariant v = args.value(QStringLiteral("enabled"));
        if (args.size() != 1 || v.metaType().id() != QMetaType::Bool) { finish(id, operation, false, {}, QStringLiteral("invalidArguments")); return; }
        m_settings->calibration()->setAutoFlowCalibration(v.toBool()); ok = true;
    } else if (operation == QLatin1String("clearProfileFlowCalibration")) {
        const QString filename = args.value(QStringLiteral("filename")).toString();
        if (args.size() != 1 || filename.isEmpty() || filename != m_profiles->baseProfileName()) { finish(id, operation, false, {}, QStringLiteral("contextChanged")); return; }
        m_settings->calibration()->clearProfileFlowCalibration(filename); ok = true;
    } else if (operation == QLatin1String("resetSawLearningForProfile")) {
        const QString filename = args.value(QStringLiteral("filename")).toString();
        const QString scale = args.value(QStringLiteral("scaleType")).toString();
        if (args.size() != 2 || filename.isEmpty() || filename != m_profiles->baseProfileName() || scale != m_machineState->activeScaleType()) { finish(id, operation, false, {}, QStringLiteral("contextChanged")); return; }
        m_settings->calibration()->resetSawLearningForProfile(filename, scale); ok = true;
    } else if (operation == QLatin1String("resetSawLearning")) {
        if (!args.isEmpty()) { finish(id, operation, false, {}, QStringLiteral("invalidArguments")); return; }
        m_settings->calibration()->resetSawLearning(); ok = true;
    } else if (operation == QLatin1String("setUseFlowScale") || operation == QLatin1String("setIgnoreVolumeWithScale") || operation == QLatin1String("setSteamTwoTapStop")) {
        const QVariant v = args.value(QStringLiteral("enabled"));
        if (args.size() != 1 || v.metaType().id() != QMetaType::Bool) { finish(id, operation, false, {}, QStringLiteral("invalidArguments")); return; }
        if (operation == QLatin1String("setUseFlowScale")) m_settings->setUseFlowScale(v.toBool());
        else if (operation == QLatin1String("setIgnoreVolumeWithScale")) m_settings->brew()->setIgnoreVolumeWithScale(v.toBool());
        else m_settings->hardware()->setSteamTwoTapStop(v.toBool());
        ok = true;
    } else if (operation == QLatin1String("clearSteamHealthHistory")) {
        if (!args.isEmpty()) { finish(id, operation, false, {}, QStringLiteral("invalidArguments")); return; }
        m_steamHealth->clearHistory(); ok = true;
    } else if (operation == QLatin1String("setHeaterValue")) {
        const QString name = args.value(QStringLiteral("name")).toString();
        const QVariant value = args.value(QStringLiteral("value"));
        bool valid = false;
        int numeric = 0;
        valid = integerValue(value, &numeric);
        if (args.size() != 2 || !valid) { m_applyingOperation = false; finish(id, operation, false, {}, QStringLiteral("invalidArguments")); return; }
        auto* h = m_settings->hardware();
        if (!CalibrationOperationPolicy::heaterValueValid(name, numeric)) { m_applyingOperation = false; finish(id, operation, false, {}, QStringLiteral("invalidValue")); return; }
        if (name == QLatin1String("heaterIdleTemp")) h->setHeaterIdleTemp(numeric);
        else if (name == QLatin1String("heaterWarmupFlow")) h->setHeaterWarmupFlow(numeric);
        else if (name == QLatin1String("heaterTestFlow")) h->setHeaterTestFlow(numeric);
        else if (name == QLatin1String("heaterWarmupTimeout")) h->setHeaterWarmupTimeout(numeric);
        else h->setFanThreshold(numeric);
        ok = true;
    } else if (operation == QLatin1String("setHeaterDefaults")) {
        if (!args.isEmpty()) { finish(id, operation, false, {}, QStringLiteral("invalidArguments")); return; }
        auto* h = m_settings->hardware();
        h->setHeaterIdleTemp(990); h->setHeaterWarmupFlow(20); h->setHeaterTestFlow(40);
        h->setHeaterWarmupTimeout(10); h->setFanThreshold(60); h->setSteamTwoTapStop(true);
        ok = true;
    } else if (operation == QLatin1String("setHeaterVoltage")) {
        const QVariant value = args.value(QStringLiteral("volts"));
        int volts = 0;
        const bool valid = integerValue(value, &volts);
        if (args.size() != 1 || !valid || !CalibrationOperationPolicy::heaterVoltageValid(volts)) { m_applyingOperation = false; finish(id, operation, false, {}, QStringLiteral("invalidValue")); return; }
        if (!m_device->isConnected()) { m_applyingOperation = false; finish(id, operation, false, {}, QStringLiteral("disconnected")); return; }
        m_device->setHeaterVoltage(volts); ok = true;
    } else {
        m_applyingOperation = false;
        finish(id, operation, false, {}, QStringLiteral("unsupportedOperation")); return;
    }
    m_applyingOperation = false;
    publish(ok || m_hostChangedDuringOperation);
    m_hostChangedDuringOperation = false;
    finish(id, operation, ok, capture(), ok ? QString() : QStringLiteral("refused"));
}
