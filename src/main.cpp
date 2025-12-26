#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QIcon>
#include <QTimer>
#include <QGuiApplication>
#include <memory>
#include "buildinfo.h"

#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QCoreApplication>
#endif

#include "core/settings.h"
#include "ble/blemanager.h"
#include "ble/de1device.h"
#include "ble/scaledevice.h"
#include "ble/scales/scalefactory.h"
#include "ble/scales/flowscale.h"
#include "machine/machinestate.h"
#include "models/shotdatamodel.h"
#include "controllers/maincontroller.h"
#include "screensaver/screensavervideomanager.h"

using namespace Qt::StringLiterals;

// Custom message handler to filter noisy BLE driver messages
static QtMessageHandler originalHandler = nullptr;
void messageFilter(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    // Filter out Windows Bluetooth driver noise
    if (msg.contains("Windows.Devices.Bluetooth") ||
        msg.contains("ReturnHr") ||
        msg.contains("LogHr")) {
        return;
    }

    // Filter out Android QtBluetoothGatt noise (check both category and message)
    const char* cat = context.category ? context.category : "";
    if (QString::fromLatin1(cat).contains("QtBluetoothGatt") ||
        msg.contains("Perform next BTLE IO") ||
        msg.contains("Performing queued job") ||
        msg.contains("BluetoothGatt")) {
        return;
    }

    if (originalHandler) {
        originalHandler(type, context, msg);
    }
}

int main(int argc, char *argv[])
{
    // Install message filter to suppress Windows BLE driver noise
    originalHandler = qInstallMessageHandler(messageFilter);

    QApplication app(argc, argv);

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
    std::unique_ptr<ScaleDevice> physicalScale;  // Physical BLE scale (when connected)
    FlowScale flowScale;  // Virtual scale using DE1 flow data (always available)
    ShotDataModel shotDataModel;
    MachineState machineState(&de1Device);
    MainController mainController(&settings, &de1Device, &machineState, &shotDataModel);
    ScreensaverVideoManager screensaverManager(&settings);

    // Set FlowScale as default (used when no physical scale connected)
    machineState.setScale(&flowScale);

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

    // Connect to any supported scale when discovered
    QObject::connect(&bleManager, &BLEManager::scaleDiscovered,
                     [&physicalScale, &flowScale, &machineState, &mainController, &engine, &bleManager, &settings](const QBluetoothDeviceInfo& device, const QString& type) {
        // Don't connect if we already have a connected scale
        if (physicalScale && physicalScale->isConnected()) {
            return;
        }

        // If we already have a scale object, just reconnect to it
        if (physicalScale) {
            qDebug() << "Reconnecting to" << type << "scale:" << device.name();
            physicalScale->connectToDevice(device);
            return;
        }

        // Create new scale object
        physicalScale = ScaleFactory::createScale(device, type);
        if (!physicalScale) {
            qWarning() << "Failed to create scale for type:" << type;
            return;
        }

        qDebug() << "Auto-connecting to" << type << "scale:" << device.name() << "at" << device.address().toString();

        // Save scale address for future direct wake connections
        settings.setScaleAddress(device.address().toString());
        settings.setScaleType(type);

        // Switch MachineState to use physical scale instead of FlowScale
        machineState.setScale(physicalScale.get());

        // Connect scale to BLEManager for auto-scan control
        bleManager.setScaleDevice(physicalScale.get());

        // Connect weight updates to MainController for graph data
        QObject::connect(physicalScale.get(), &ScaleDevice::weightChanged,
                         &mainController, &MainController::onScaleWeightChanged);

        // Log scale weight during shots
        QObject::connect(physicalScale.get(), &ScaleDevice::weightChanged,
                         [&physicalScale, &machineState]() {
            if (physicalScale && machineState.isFlowing()) {
                qDebug().nospace()
                    << "SCALE [" << physicalScale->name() << "] weight:" << QString::number(physicalScale->weight(), 'f', 1) << "g "
                    << "flow:" << QString::number(physicalScale->flowRate(), 'f', 2) << "g/s";
            }
        });

        // When physical scale disconnects, fall back to FlowScale
        QObject::connect(physicalScale.get(), &ScaleDevice::connectedChanged,
                         [&physicalScale, &flowScale, &machineState]() {
            if (physicalScale && !physicalScale->isConnected()) {
                qDebug() << "Physical scale disconnected, falling back to FlowScale";
                machineState.setScale(&flowScale);
            }
        });

        // Update QML context when scale is created
        QQmlContext* context = engine.rootContext();
        context->setContextProperty("ScaleDevice", physicalScale.get());

        // Connect to the scale
        physicalScale->connectToDevice(device);
    });

    // Load saved scale address for direct wake connection
    QString savedScaleAddr = settings.scaleAddress();
    QString savedScaleType = settings.scaleType();
    if (!savedScaleAddr.isEmpty() && !savedScaleType.isEmpty()) {
        bleManager.setSavedScaleAddress(savedScaleAddr, savedScaleType);
    }

    // BLE scanning is now started from QML after first-run dialog is dismissed
    // This allows the user to turn on their scale before we start scanning

    // Connect FlowScale weight updates to MainController (for when no physical scale)
    QObject::connect(&flowScale, &ScaleDevice::weightChanged,
                     &mainController, &MainController::onScaleWeightChanged);

    // Expose C++ objects to QML
    QQmlContext* context = engine.rootContext();
    context->setContextProperty("Settings", &settings);
    context->setContextProperty("BLEManager", &bleManager);
    context->setContextProperty("DE1Device", &de1Device);
    context->setContextProperty("ScaleDevice", &flowScale);  // FlowScale initially, updated when physical scale connects
    context->setContextProperty("MachineState", &machineState);
    context->setContextProperty("ShotDataModel", &shotDataModel);
    context->setContextProperty("MainController", &mainController);
    context->setContextProperty("ScreensaverManager", &screensaverManager);
    context->setContextProperty("BuildNumber", BUILD_NUMBER_STRING);

    // Register types for QML (use different names to avoid conflict with context properties)
    qmlRegisterUncreatableType<DE1Device>("DE1App", 1, 0, "DE1DeviceType",
        "DE1Device is created in C++");
    qmlRegisterUncreatableType<MachineState>("DE1App", 1, 0, "MachineStateType",
        "MachineState is created in C++");

    // Load main QML file (QTP0001 NEW policy uses /qt/qml/ prefix)
    const QUrl url(u"qrc:/qt/qml/DE1App/qml/main.qml"_s);

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
        &app, [url](QObject *obj, const QUrl &objUrl) {
            if (!obj && url == objUrl)
                QCoreApplication::exit(-1);
        }, Qt::QueuedConnection);

    engine.load(url);

#ifdef Q_OS_ANDROID
    // Force landscape orientation on Android (after QML is loaded)
    // SCREEN_ORIENTATION_LANDSCAPE = 0 (forced landscape)
    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (activity.isValid()) {
        activity.callMethod<void>("setRequestedOrientation", "(I)V", 0);
    }
#endif

    // Cross-platform lifecycle handling: sleep/wake devices when app is suspended/resumed
    // This handles cases like swiping away from Recent Apps (Android/iOS) or minimizing (desktop)
    QObject::connect(&app, &QGuiApplication::applicationStateChanged,
                     [&de1Device, &physicalScale, &bleManager, &settings](Qt::ApplicationState state) {
        static bool wasSuspended = false;

        if (state == Qt::ApplicationSuspended) {
            // App is being suspended (mobile) - sleep devices immediately
            qDebug() << "App suspended - putting devices to sleep";
            wasSuspended = true;

            if (physicalScale && physicalScale->isConnected()) {
                physicalScale->sleep();
            }
            if (de1Device.isConnected()) {
                de1Device.goToSleep();
            }
        }
        else if (state == Qt::ApplicationActive && wasSuspended) {
            // App resumed from suspended state - wake devices
            qDebug() << "App resumed - waking devices";
            wasSuspended = false;

            // Wake DE1 (it wakes automatically on reconnect, but ensure it's awake)
            if (de1Device.isConnected()) {
                de1Device.wakeUp();
            }

            // Try to reconnect/wake scale
            if (physicalScale && physicalScale->isConnected()) {
                physicalScale->wake();
            } else if (!settings.scaleAddress().isEmpty()) {
                // Scale disconnected while suspended - try to reconnect
                QTimer::singleShot(500, &bleManager, &BLEManager::tryDirectConnectToScale);
            }
        }
    });

    return app.exec();
}
