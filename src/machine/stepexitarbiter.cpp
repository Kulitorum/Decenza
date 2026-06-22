#include "stepexitarbiter.h"

#include <QDebug>
#include <cmath>

FrameExitCondition FrameExitCondition::fromExitFields(bool exitIf, const QString& exitType,
                                                      double pressureOver, double pressureUnder,
                                                      double flowOver, double flowUnder)
{
    if (!exitIf)
        return {};  // no firmware exit on this frame

    if (exitType == QLatin1String("pressure_over"))
        return {Kind::PressureOver, pressureOver};
    if (exitType == QLatin1String("pressure_under"))
        return {Kind::PressureUnder, pressureUnder};
    if (exitType == QLatin1String("flow_over"))
        return {Kind::FlowOver, flowOver};
    if (exitType == QLatin1String("flow_under"))
        return {Kind::FlowUnder, flowUnder};

    // "weight" is an app-side exit (handled separately), so it correctly maps to
    // None here. Anything else with exitIf set is unexpected — log it, since it
    // silently disables the double-skip guard for that frame.
    if (exitType != QLatin1String("weight")) {
        qWarning() << "[StepExitArbiter] exitIf set with unrecognized exitType"
                   << exitType << "— frame treated as weight-only (no arbitration)";
    }
    return {};
}

StepExitArbiter::Verdict StepExitArbiter::evaluate(int profileFrame,
                                                   const FrameExitCondition& exit,
                                                   double currentPressure,
                                                   double currentFlow)
{
    // Non-actionable firmware exit (e.g. pressure-over 0) — treat as weight-only.
    if (!exit.isActionable()) {
        return Verdict::Fire;
    }

    const bool isPressure = exit.isPressure();
    const double sensorValue = isPressure ? currentPressure : currentFlow;

    const double fraction = isPressure ? kPressureProximityFraction : kFlowProximityFraction;
    const double minimum = isPressure ? kPressureProximityMinimum : kFlowProximityMinimum;
    const double window = std::max(fraction * exit.value, minimum);

    const double distance = std::abs(exit.value - sensorValue);

    // Far from the firmware threshold — no race risk, fire now.
    if (distance > window) {
        m_deferrals.remove(profileFrame);
        qDebug() << "[StepExitArbiter] frame" << profileFrame
                 << "sensor" << sensorValue << "far from exit" << exit.value
                 << "(window" << window << ") — FIRE";
        return Verdict::Fire;
    }

    // Near the firmware threshold — track deferral.
    DeferralState& deferral = m_deferrals[profileFrame];
    deferral.record(sensorValue);

    if (deferral.count() >= kMaxDeferralSamples) {
        qDebug() << "[StepExitArbiter] frame" << profileFrame
                 << "max deferral (" << kMaxDeferralSamples << ") reached — FIRE";
        return Verdict::Fire;
    }

    if (deferral.isTrending(exit.isOver())) {
        qDebug() << "[StepExitArbiter] frame" << profileFrame
                 << "near exit" << exit.value << "(distance" << distance
                 << ") and trending — DEFER" << deferral.count() << "/"
                 << kMaxDeferralSamples;
        return Verdict::Defer;
    }

    qDebug() << "[StepExitArbiter] frame" << profileFrame
             << "near exit" << exit.value << "but NOT trending — FIRE";
    return Verdict::Fire;
}

void StepExitArbiter::onFrameAdvanced(int newFrame)
{
    for (auto it = m_deferrals.begin(); it != m_deferrals.end();) {
        if (it.key() < newFrame)
            it = m_deferrals.erase(it);
        else
            ++it;
    }
}

void StepExitArbiter::reset()
{
    m_deferrals.clear();
}

bool StepExitArbiter::DeferralState::isTrending(bool exitIsOver) const
{
    if (readings.size() < 2)
        return true;  // first sample: assume trending toward firmware exit
    for (qsizetype i = readings.size() - 1; i >= 1; --i) {
        const double prev = readings[i - 1];
        const double curr = readings[i];
        const bool stepTowards = exitIsOver ? (curr > prev) : (curr < prev);
        if (!stepTowards)
            return false;
    }
    return true;
}
