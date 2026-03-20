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
}
