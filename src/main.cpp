#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QIcon>
#include <QTimer>
#include <QGuiApplication>
#include <QDebug>
#include <memory>
#include "buildinfo.h"

#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QCoreApplication>
#endif


#include "core/settings.h"
#include "core/batterymanager.h"
#include "core/batterydrainer.h"
#include "core/accessibilitymanager.h"
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

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Set application metadata
    app.setOrganizationName("DecentEspresso");
    app.setOrganizationDomain("decentespresso.com");
    app.setApplicationName("Decenza DE1");
    app.setApplicationVersion("1.0.0");

    // Use Material style for modern look
    QQuickStyle::setStyle("Material");

    qDebug() << "App started - build" << BUILD_NUMBER_STRING;

    // Create core objects
    Settings settings;
    BLEManager bleManager;
    DE1Device de1Device;
    std::unique_ptr<ScaleDevice> physicalScale;  // Physical BLE scale (when connected)
    FlowScale flowScale;  // Virtual scale using DE1 flow data (fallback when no BLE scale)
    ShotDataModel shotDataModel;
    MachineState machineState(&de1Device);
    machineState.setSettings(&settings);
    machineState.setScale(&flowScale);  // Start with FlowScale, switch to physical scale if found
    flowScale.setSettings(&settings);
    MainController mainController(&settings, &de1Device, &machineState, &shotDataModel);

    // Connect FlowScale to graph initially (will be disconnected if physical scale found)
    QObject::connect(&flowScale, &ScaleDevice::weightChanged,
                     &mainController, &MainController::onScaleWeightChanged);

    ScreensaverVideoManager screensaverManager(&settings);
    BatteryManager batteryManager;
    batteryManager.setDE1Device(&de1Device);
    batteryManager.setSettings(&settings);
    BatteryDrainer batteryDrainer;
    AccessibilityManager accessibilityManager;

    // FlowScale fallback timer - notify after 30 seconds if no physical scale found
    QTimer flowScaleFallbackTimer;
    flowScaleFallbackTimer.setSingleShot(true);
    flowScaleFallbackTimer.setInterval(30000);  // 30 seconds
    QObject::connect(&flowScaleFallbackTimer, &QTimer::timeout,
                     [&physicalScale, &bleManager]() {
        if (!physicalScale || !physicalScale->isConnected()) {
            // No physical scale found - FlowScale is already active, just notify UI
            emit bleManager.flowScaleFallback();
        }
    });
    // Start timer only when scanning actually begins (after first-run dialog)
    QObject::connect(&bleManager, &BLEManager::scanStarted, &flowScaleFallbackTimer, [&flowScaleFallbackTimer]() {
        if (!flowScaleFallbackTimer.isActive()) {
            flowScaleFallbackTimer.start();
        }
    });

    // Set up QML engine
    QQmlApplicationEngine engine;

    // Auto-connect when DE1 is discovered
    QObject::connect(&bleManager, &BLEManager::de1Discovered,
                     &de1Device, [&de1Device](const QBluetoothDeviceInfo& device) {
        if (!de1Device.isConnected() && !de1Device.isConnecting()) {
            de1Device.connectToDevice(device);
        }
    });

    // Connect to any supported scale when discovered
    QObject::connect(&bleManager, &BLEManager::scaleDiscovered,
                     [&physicalScale, &flowScale, &machineState, &mainController, &engine, &bleManager, &settings, &flowScaleFallbackTimer](const QBluetoothDeviceInfo& device, const QString& type) {
        // Don't connect if we already have a connected scale
        if (physicalScale && physicalScale->isConnected()) {
            return;
        }

        // If we already have a scale object, just reconnect to it
        if (physicalScale) {
            flowScaleFallbackTimer.stop();  // Stop timer - we found a scale
            // Re-wire to use physical scale
            machineState.setScale(physicalScale.get());
            engine.rootContext()->setContextProperty("ScaleDevice", physicalScale.get());
            physicalScale->connectToDevice(device);
            return;
        }

        // Create new scale object
        physicalScale = ScaleFactory::createScale(device, type);
        if (!physicalScale) {
            qWarning() << "Failed to create scale for type:" << type;
            return;
        }

        // Stop the FlowScale fallback timer since we found a physical scale
        flowScaleFallbackTimer.stop();

        // Save scale address for future direct wake connections
        settings.setScaleAddress(device.address().toString());
        settings.setScaleType(type);

        // Switch MachineState to use physical scale instead of FlowScale
        machineState.setScale(physicalScale.get());

        // Connect scale to BLEManager for auto-scan control
        bleManager.setScaleDevice(physicalScale.get());

        // Disconnect FlowScale from graph (in case fallback was active)
        QObject::disconnect(&flowScale, &ScaleDevice::weightChanged,
                            &mainController, &MainController::onScaleWeightChanged);

        // Connect physical scale weight updates to MainController for graph data
        QObject::connect(physicalScale.get(), &ScaleDevice::weightChanged,
                         &mainController, &MainController::onScaleWeightChanged);

        // When physical scale connects/disconnects, switch between physical and FlowScale
        QObject::connect(physicalScale.get(), &ScaleDevice::connectedChanged,
                         [&physicalScale, &flowScale, &machineState, &engine, &bleManager, &mainController]() {
            if (physicalScale && physicalScale->isConnected()) {
                // Scale connected - use physical scale
                machineState.setScale(physicalScale.get());
                engine.rootContext()->setContextProperty("ScaleDevice", physicalScale.get());
                // Disconnect FlowScale from graph to prevent duplicate data
                QObject::disconnect(&flowScale, &ScaleDevice::weightChanged,
                                    &mainController, &MainController::onScaleWeightChanged);
                qDebug() << "Scale connected - switched to physical scale";
            } else if (physicalScale) {
                // Scale disconnected - fall back to FlowScale
                machineState.setScale(&flowScale);
                engine.rootContext()->setContextProperty("ScaleDevice", &flowScale);
                // Reconnect FlowScale to graph
                QObject::connect(&flowScale, &ScaleDevice::weightChanged,
                                 &mainController, &MainController::onScaleWeightChanged);
                emit bleManager.scaleDisconnected();
                qDebug() << "Scale disconnected - switched to FlowScale";
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

    // FlowScale weight connection is handled by the fallback timer and scale disconnect logic
    // Don't connect here - only one scale should feed the graph at a time

    // Expose C++ objects to QML
    QQmlContext* context = engine.rootContext();
    context->setContextProperty("Settings", &settings);
    context->setContextProperty("BLEManager", &bleManager);
    context->setContextProperty("DE1Device", &de1Device);
    context->setContextProperty("ScaleDevice", &flowScale);  // FlowScale initially, updated when physical scale connects
    context->setContextProperty("FlowScale", &flowScale);  // Always available for calibration
    context->setContextProperty("MachineState", &machineState);
    context->setContextProperty("ShotDataModel", &shotDataModel);
    context->setContextProperty("MainController", &mainController);
    context->setContextProperty("ScreensaverManager", &screensaverManager);
    context->setContextProperty("BatteryManager", &batteryManager);
    context->setContextProperty("BatteryDrainer", &batteryDrainer);
    context->setContextProperty("AccessibilityManager", &accessibilityManager);
    context->setContextProperty("BuildNumber", BUILD_NUMBER_STRING);
#ifdef QT_DEBUG
    context->setContextProperty("IsDebugBuild", true);
#else
    context->setContextProperty("IsDebugBuild", false);
#endif

    // Register types for QML (use different names to avoid conflict with context properties)
    qmlRegisterUncreatableType<DE1Device>("DecenzaDE1", 1, 0, "DE1DeviceType",
        "DE1Device is created in C++");
    qmlRegisterUncreatableType<MachineState>("DecenzaDE1", 1, 0, "MachineStateType",
        "MachineState is created in C++");

    // Load main QML file (QTP0001 NEW policy uses /qt/qml/ prefix)
    const QUrl url(u"qrc:/qt/qml/DecenzaDE1/qml/main.qml"_s);

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
        &app, [url](QObject *obj, const QUrl &objUrl) {
            if (!obj && url == objUrl)
                QCoreApplication::exit(-1);
        }, Qt::QueuedConnection);

    engine.load(url);

#ifdef Q_OS_ANDROID
    // Set landscape orientation on Android (after QML is loaded)
    // SCREEN_ORIENTATION_SENSOR_LANDSCAPE = 6 (uses sensor to pick correct landscape)
    // Note: Using 0 (SCREEN_ORIENTATION_LANDSCAPE) causes upside-down display on some tablets
    // because "natural landscape" varies by device manufacturer
    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (activity.isValid()) {
        activity.callMethod<void>("setRequestedOrientation", "(I)V", 6);

        // Enable immersive mode - must run on UI thread
        QNativeInterface::QAndroidApplication::runOnAndroidMainThread([activity]() {
            QJniObject window = activity.callObjectMethod("getWindow", "()Landroid/view/Window;");
            if (window.isValid()) {
                // FLAG_LAYOUT_NO_LIMITS = 0x200 - extend window into navigation bar area
                window.callMethod<void>("addFlags", "(I)V", 0x200);

                // Immersive sticky mode flags
                // IMMERSIVE_STICKY | FULLSCREEN | HIDE_NAVIGATION | LAYOUT_STABLE | LAYOUT_HIDE_NAVIGATION | LAYOUT_FULLSCREEN
                // 0x1000 | 0x4 | 0x2 | 0x100 | 0x200 | 0x400 = 0x1706
                QJniObject decorView = window.callObjectMethod("getDecorView", "()Landroid/view/View;");
                if (decorView.isValid()) {
                    decorView.callMethod<void>("setSystemUiVisibility", "(I)V", 0x1706);
                }
            }
        });
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

    // Cleanup on exit
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&accessibilityManager]() {
        // Shutdown accessibility to stop TTS before any other cleanup
        // This prevents race conditions with Android's hwuiTask thread
        accessibilityManager.shutdown();
    });

    return app.exec();
}
