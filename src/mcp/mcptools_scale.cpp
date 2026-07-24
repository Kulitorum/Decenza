#include "mcpserver.h"
#include "mcptoolregistry.h"
#include "../machine/machinestate.h"
#include "../ble/scaledevice.h"

#include <QJsonObject>
#include <QMetaObject>

void registerScaleTools(McpToolRegistry* registry, MachineState* machineState)
{
    // scale_tare
    registry->registerTool(
        "scale_tare",
        "Tare (zero) the connected scale",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
        [machineState](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            if (!machineState || !machineState->scale()) {
                result["error"] = "No scale connected";
                return result;
            }
            QMetaObject::invokeMethod(machineState->scale(), "tare", Qt::QueuedConnection);
            result["success"] = true;
            result["message"] = "Scale tared";
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
            if (!machineState || !machineState->scale()) {
                result["error"] = "No scale connected";
                return result;
            }
            QMetaObject::invokeMethod(machineState->scale(), "startTimer", Qt::QueuedConnection);
            result["success"] = true;
            result["message"] = "Timer started";
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
            if (!machineState || !machineState->scale()) {
                result["error"] = "No scale connected";
                return result;
            }
            QMetaObject::invokeMethod(machineState->scale(), "stopTimer", Qt::QueuedConnection);
            result["success"] = true;
            result["message"] = "Timer stopped";
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
            if (!machineState || !machineState->scale()) {
                result["error"] = "No scale connected";
                return result;
            }
            QMetaObject::invokeMethod(machineState->scale(), "resetTimer", Qt::QueuedConnection);
            result["success"] = true;
            result["message"] = "Timer reset";
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
