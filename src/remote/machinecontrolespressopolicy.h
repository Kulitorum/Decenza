#pragma once

#include "../core/machinecontrolstartpolicy.h"
#include "../machine/machinephase.h"

#include <QString>
#include <cmath>

namespace MachineControlEspressoPolicy {

inline bool isStopPhase(MachinePhase phase)
{
    return phase == MachinePhase::EspressoPreheating
        || phase == MachinePhase::Preinfusion
        || phase == MachinePhase::Pouring
        || phase == MachinePhase::Ending;
}

inline bool isLiveExtractionPhase(MachinePhase phase)
{
    return phase == MachinePhase::Preinfusion || phase == MachinePhase::Pouring;
}

inline bool stopAllowed(const QString& expectedContext, const QString& currentContext,
                        MachinePhase phase, bool connected)
{
    return MachineControlStartPolicy::contextMatches(expectedContext, currentContext)
        && connected && isStopPhase(phase);
}

inline bool targetAdjustmentAllowed(const QString& expectedContext,
                                    const QString& currentContext,
                                    MachinePhase phase, bool connected,
                                    double targetWeightG, bool canDecrease,
                                    double deltaG)
{
    const bool supportedDelta = deltaG == -10.0 || deltaG == -5.0
        || deltaG == 5.0 || deltaG == 10.0;
    return MachineControlStartPolicy::contextMatches(expectedContext, currentContext)
        && connected && isLiveExtractionPhase(phase)
        && std::isfinite(targetWeightG) && targetWeightG > 0.0
        && std::isfinite(deltaG) && supportedDelta
        && (deltaG > 0.0 || canDecrease);
}

inline bool skipAllowed(const QString& expectedContext, const QString& currentContext,
                        MachinePhase phase, bool connected)
{
    return MachineControlStartPolicy::contextMatches(expectedContext, currentContext)
        && connected && isLiveExtractionPhase(phase);
}

} // namespace MachineControlEspressoPolicy
