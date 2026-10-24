#pragma once

#include "remotemachineuisource.h"
#include "../profile/profiledialintext.h"
#include <QVariantList>
#include <QVariantMap>
#include <type_traits>

class QQmlEngine;
class QJSEngine;

// The shared readouts retain their native ProfileManager expressions; the host
// resolves the active profile and ratio target before publishing these values.
class RemoteProfileManager final : public RemoteMachineUiSource
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ProfileManager)
    QML_SINGLETON

    Q_PROPERTY(double profileTargetTemperature READ profileTargetTemperature NOTIFY machineUiChanged FINAL)
    Q_PROPERTY(double profileTargetWeight READ profileTargetWeight NOTIFY machineUiChanged FINAL)
    Q_PROPERTY(double targetWeight READ targetWeight NOTIFY machineUiChanged FINAL)
    Q_PROPERTY(bool brewByRatioActive READ brewByRatioActive NOTIFY machineUiChanged FINAL)
    Q_PROPERTY(double brewByRatio READ brewByRatio NOTIFY machineUiChanged FINAL)
    Q_PROPERTY(double brewByRatioDose READ brewByRatioDose NOTIFY machineUiChanged FINAL)
    Q_PROPERTY(QVariantMap dialInLabels READ dialInLabels CONSTANT FINAL)

public:
    static RemoteProfileManager* create(QQmlEngine* engine, QJSEngine* jsEngine);
    double profileTargetTemperature() const { return machineUi().profileTargetTemperature; }
    double profileTargetWeight() const { return machineUi().profileTargetWeight; }
    double targetWeight() const { return machineUi().profileEffectiveTargetWeight; }
    bool brewByRatioActive() const { return machineUi().profileBrewByRatioActive; }
    double brewByRatio() const { return machineUi().profileBrewByRatio; }
    double brewByRatioDose() const { return machineUi().profileBrewByRatioDose; }
    QVariantMap dialInLabels() const { return ProfileDialInText::labelMap(); }
    Q_INVOKABLE QString temperatureDisplayForSteps(const QVariantList& stepTempsC,
                                                    double anchorTemp, bool hasOverride,
                                                    double overrideTemp,
                                                    double baselineShiftC = 0.0) const;

private:
    explicit RemoteProfileManager(QObject* parent);
};

static_assert(!std::is_default_constructible_v<RemoteProfileManager>,
              "The QML singleton must use its engine-owned factory.");
