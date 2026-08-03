#include "mcpserver.h"
#include "mcptoolregistry.h"
#include "../machine/machinestate.h"
#include "../ble/scaledevice.h"

#include <QJsonObject>
#include <QMetaObject>

namespace {

// Shared precondition for the three scale_timer_* tools: a scale must be
// connected AND must actually implement the timer. The three slots are virtual
// with empty default bodies, so a scale that does not implement them accepts
// every command and does nothing — which is what these tools used to report as
// success, on every scale, including ones whose own headers say they have no
// remote timer control.
//
// One function rather than the same block three times: three copies of a
// precondition are three chances for one of them to drift.
//
// Returns true when the call must not proceed, having written `error` into
// `result`.
// Is there a real, connected scale to command at all? Shared by every tool here,
// because none of them can do anything useful without one.
bool scaleUnavailable(MachineState* machineState, QJsonObject& result)
{
    ScaleDevice* scale = machineState ? machineState->scale() : nullptr;

    // `!scale` is defensive only. MachineState::m_scale is set to the FlowScale
    // at startup and is never null afterwards — on disconnect it is swapped BACK
    // to the FlowScale, not cleared. So a bare null check is not the "is a scale
    // connected" test it looks like, and writing it that way is how the first
    // version of this helper ended up telling a user with no scale at all that
    // "This scale (Flow Scale) does not support remote timer control" — a
    // hardware limitation attributed to a scale that does not exist.
    if (!scale) {
        result["error"] = "No scale is available";
        return true;
    }
    if (scale->isFlowScale()) {
        result["error"] = "No physical scale is connected — weight is being estimated "
                          "from DE1 flow, and there is no scale timer to drive.";
        return true;
    }
    if (!scale->isConnected()) {
        result["error"] = "The scale (" + scale->name() + ") is not connected";
        return true;
    }
    return false;
}

// The timer preconditions: a usable scale, plus a driver that actually sends
// timer commands. Separate from scaleUnavailable so scale_tare shares the first
// half without inheriting the second.
bool timerUnavailable(MachineState* machineState, QJsonObject& result)
{
    if (scaleUnavailable(machineState, result))
        return true;
    if (!machineState->scale()->supportsTimer()) {
        result["error"] = "This scale (" + machineState->scale()->name()
                          + ") does not support remote timer control";
        return true;
    }
    return false;
}

}  // namespace

void registerScaleTools(McpToolRegistry* registry, MachineState* machineState)
{
    // scale_tare
    registry->registerTool(
        "scale_tare",
        "Tare (zero) the connected scale",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
        [machineState](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            // Same ladder as the timer tools: a bare null check is not a
            // connectivity test here (see scaleUnavailable), so scale_tare used
            // to report a successful tare of the FlowScale when no scale existed.
            if (scaleUnavailable(machineState, result))
                return result;
            QMetaObject::invokeMethod(machineState->scale(), "tare", Qt::QueuedConnection);
            result["success"] = true;
            result["message"] = "Tare command sent to the scale";
            return result;
        },
        "control");

    // scale_timer_start
    registry->registerTool(
        "scale_timer_start",
        "Start the scale's built-in timer (if supported by the scale)",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
        [machineState](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            if (timerUnavailable(machineState, result))
                return result;
            QMetaObject::invokeMethod(machineState->scale(), "startTimer", Qt::QueuedConnection);
            result["success"] = true;
            result["message"] = "Timer start command sent to the scale";
            return result;
        },
        "control");

    // scale_timer_stop
    registry->registerTool(
        "scale_timer_stop",
        "Stop the scale's built-in timer",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
        [machineState](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            if (timerUnavailable(machineState, result))
                return result;
            QMetaObject::invokeMethod(machineState->scale(), "stopTimer", Qt::QueuedConnection);
            result["success"] = true;
            result["message"] = "Timer stop command sent to the scale";
            return result;
        },
        "control");

    // scale_timer_reset
    registry->registerTool(
        "scale_timer_reset",
        "Reset the scale's built-in timer to zero",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
        [machineState](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            if (timerUnavailable(machineState, result))
                return result;
            // A scale can support the timer and still have no independent RESET:
            // DiFluid's resetTimer() sends the same bytes as startTimer(), which
            // is what hasIndependentTimerReset() reports. Passing the capability
            // gate and then answering "Timer reset" would be the same fiction the
            // gate was added to stop, one step narrower — the timer starts.
            if (!machineState->scale()->hasIndependentTimerReset()) {
                result["error"] = "This scale (" + machineState->scale()->name()
                                  + ") cannot reset its timer independently — the reset "
                                    "command also starts it. Use scale_timer_stop first, "
                                    "or start a fresh timer with scale_timer_start.";
                return result;
            }
            QMetaObject::invokeMethod(machineState->scale(), "resetTimer", Qt::QueuedConnection);
            result["success"] = true;
            result["message"] = "Timer reset command sent to the scale";
            return result;
        },
        "control");

    // scale_get_weight
    registry->registerTool(
        "scale_get_weight",
        "Get the current weight reading and flow rate from the connected scale",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
        [machineState](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            if (!machineState || !machineState->scale()) {
                result["error"] = "No scale connected";
                return result;
            }
            result["weightG"] = machineState->scaleWeight();
            result["flowRateMlPerSec"] = machineState->scaleFlowRate();
            result["smoothedFlowRateMlPerSec"] = machineState->smoothedScaleFlowRate();
            // name()/type(), not objectName(): no scale driver ever calls
            // setObjectName(), so objectName() reported "" for every scale —
            // including the simulated one — which reads as "no scale attached"
            // to a model looking at this response.
            result["scaleName"] = machineState->scale()->name();
            result["scaleType"] = machineState->scale()->type();
            result["connected"] = machineState->scale()->isConnected();
            return result;
        },
        "read");
}
