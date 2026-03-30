#include "mcpserver.h"
#include "mcptoolregistry.h"
#include "../ble/blemanager.h"
#include "../ble/de1device.h"

#include <QJsonObject>
#include <QJsonArray>
#include <QMetaObject>

void registerDeviceTools(McpToolRegistry* registry, BLEManager* bleManager, DE1Device* device)
{
    // devices_list
    registry->registerTool(
        "devices_list",
        "List discovered BLE devices (DE1 machines and scales found during scanning)",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
        [bleManager](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            if (!bleManager) {
                result["error"] = "BLE manager not available";
                return result;
            }
            QVariantList devices = bleManager->discoveredDevices();
            QJsonArray devArr;
            for (const QVariant& v : devices) {
                QVariantMap dm = v.toMap();
                QJsonObject dev;
                dev["address"] = dm["address"].toString();
                dev["name"] = dm["name"].toString();
                dev["type"] = dm["type"].toString();
                dev["rssi"] = dm["rssi"].toInt();
                devArr.append(dev);
            }
            result["devices"] = devArr;
            result["count"] = devArr.size();
            return result;
        },
        "read");

    // devices_scan
    registry->registerTool(
        "devices_scan",
        "Start scanning for BLE devices (DE1 machines and scales). Results appear in devices_list after a few seconds.",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
        [bleManager](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            if (!bleManager) {
                result["error"] = "BLE manager not available";
                return result;
            }
            QMetaObject::invokeMethod(bleManager, "startScan", Qt::QueuedConnection);
            result["success"] = true;
            result["message"] = "BLE scan started. Call devices_list after a few seconds to see results.";
            return result;
        },
        "control");

    // devices_connect_scale
    registry->registerTool(
        "devices_connect_scale",
        "Connect to a scale by its BLE address. Use devices_list to find available scales.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"address", QJsonObject{{"type", "string"}, {"description", "BLE address of the scale to connect"}}}
            }},
            {"required", QJsonArray{"address"}}
        },
        [bleManager](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            if (!bleManager) {
                result["error"] = "BLE manager not available";
                return result;
            }
            QString address = args["address"].toString();
            if (address.isEmpty()) {
                result["error"] = "address is required";
                return result;
            }
            QMetaObject::invokeMethod(bleManager, "connectToScale",
                Qt::QueuedConnection, Q_ARG(QString, address));
            result["success"] = true;
            result["message"] = "Connecting to scale at " + address;
            return result;
        },
        "control");

    // devices_connection_status
    registry->registerTool(
        "devices_connection_status",
        "Get connection status of the DE1 machine and scale",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
        [device, bleManager](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            if (device) {
                result["machineConnected"] = device->isConnected();
                result["machineAddress"] = device->isConnected() ? "connected" : "disconnected";
            }
            result["bleAvailable"] = bleManager != nullptr;
            return result;
        },
        "read");

    // devices_connect_de1
    registry->registerTool(
        "devices_connect_de1",
        "Connect to a DE1 machine by its BLE address. Use devices_list to find available machines.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"address", QJsonObject{{"type", "string"}, {"description", "BLE address of the DE1 to connect"}}}
            }},
            {"required", QJsonArray{"address"}}
        },
        [device](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            if (!device) {
                result["error"] = "DE1 device not available";
                return result;
            }
            QString address = args["address"].toString();
            if (address.isEmpty()) {
                result["error"] = "address is required";
                return result;
            }
            if (device->isConnected()) {
                result["message"] = "Already connected to a DE1 machine";
                return result;
            }
            QMetaObject::invokeMethod(device, "connectToDevice",
                Qt::QueuedConnection, Q_ARG(QString, address));
            result["success"] = true;
            result["message"] = "Connecting to DE1 at " + address;
            return result;
        },
        "control");

    // devices_disconnect_scale
    registry->registerTool(
        "devices_disconnect_scale",
        "Disconnect and forget the currently connected BLE scale. "
        "The scale will need to be re-selected via devices_connect_scale.",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{
            {"confirmed", QJsonObject{{"type", "boolean"}, {"description", "Set to true after user confirms this action in chat"}}}
        }}},
        [bleManager](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            if (!bleManager) {
                result["error"] = "BLE manager not available";
                return result;
            }
            bleManager->clearSavedScale();
            result["success"] = true;
            result["message"] = "Scale disconnected and forgotten";
            return result;
        },
        "control");
}
