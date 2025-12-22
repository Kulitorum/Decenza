#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QIcon>
#include <QTimer>

#include "core/settings.h"
#include "ble/blemanager.h"
#include "ble/de1device.h"
#include "ble/scaledevice.h"
#include "machine/machinestate.h"
#include "models/shotdatamodel.h"
#include "controllers/maincontroller.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    // Set application metadata
    app.setOrganizationName("DecentEspresso");
    app.setOrganizationDomain("decentespresso.com");
    app.setApplicationName("DE1 Controller");
    app.setApplicationVersion("1.0.0");

    // Use Material style for modern look
    QQuickStyle::setStyle("Material");

    // Create core objects
    Settings settings;
    BLEManager bleManager;
    DE1Device de1Device;
    ShotDataModel shotDataModel;
    MachineState machineState(&de1Device);
    MainController mainController(&settings, &de1Device, &machineState, &shotDataModel);

    // Set up QML engine
    QQmlApplicationEngine engine;

    // Auto-connect when DE1 is discovered
    QObject::connect(&bleManager, &BLEManager::de1Discovered,
                     &de1Device, [&de1Device](const QBluetoothDeviceInfo& device) {
        if (!de1Device.isConnected() && !de1Device.isConnecting()) {
            qDebug() << "Auto-connecting to DE1:" << device.name();
            de1Device.connectToDevice(device);
        }
    });

    // Auto-start scanning on launch
    QTimer::singleShot(500, &bleManager, &BLEManager::startScan);

    // Expose C++ objects to QML
    QQmlContext* context = engine.rootContext();
    context->setContextProperty("Settings", &settings);
    context->setContextProperty("BLEManager", &bleManager);
    context->setContextProperty("DE1Device", &de1Device);
    context->setContextProperty("MachineState", &machineState);
    context->setContextProperty("ShotDataModel", &shotDataModel);
    context->setContextProperty("MainController", &mainController);

    // Register types for QML (use different names to avoid conflict with context properties)
    qmlRegisterUncreatableType<DE1Device>("DE1App", 1, 0, "DE1DeviceType",
        "DE1Device is created in C++");
    qmlRegisterUncreatableType<MachineState>("DE1App", 1, 0, "MachineStateType",
        "MachineState is created in C++");

    // Load main QML file (QTP0001 NEW policy uses /qt/qml/ prefix)
    const QUrl url(u"qrc:/qt/qml/DE1App/qml/main.qml"_qs);

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
        &app, [url](QObject *obj, const QUrl &objUrl) {
            if (!obj && url == objUrl)
                QCoreApplication::exit(-1);
        }, Qt::QueuedConnection);

    engine.load(url);

    return app.exec();
}
