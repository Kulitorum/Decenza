#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QSettings>
#include <QIcon>
#include <QTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QAccessible>
#include <QDebug>
#include <memory>
#include "version.h"

#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QCoreApplication>
#endif



#include "core/settings.h"
#include "core/translationmanager.h"
#include "core/batterymanager.h"
#include "core/accessibilitymanager.h"
#include "core/autowakemanager.h"
#include "core/crashhandler.h"
#include "network/crashreporter.h"
#include "core/profilestorage.h"
#include "ble/blemanager.h"
#include "ble/de1device.h"
#include "ble/scaledevice.h"
#include "ble/scales/scalefactory.h"
#include "ble/scales/flowscale.h"
#include "machine/machinestate.h"
#include "models/shotdatamodel.h"
#include "controllers/maincontroller.h"
#include "controllers/shottimingcontroller.h"
#include "ai/aimanager.h"
#include "ai/aiconversation.h"
#include "screensaver/screensavervideomanager.h"
#include "screensaver/pipegeometry.h"
#include "screensaver/strangeattractorrenderer.h"
#include "network/webdebuglogger.h"

// GHC Simulator for Windows debug builds
#if (defined(Q_OS_WIN) || defined(Q_OS_MACOS)) && defined(QT_DEBUG)
#include "simulator/ghcsimulator.h"
#include "simulator/de1simulator.h"
#include "simulator/simulatedscale.h"
#endif

using namespace Qt::StringLiterals;

int main(int argc, char *argv[])
{
    // Install crash handler first - catches SIGSEGV, SIGABRT, etc.
    CrashHandler::install();

    // Set simple message pattern for desktop (Android uses its own format)
#ifndef Q_OS_ANDROID
    qSetMessagePattern("%{message}");
#endif

#ifdef Q_OS_IOS
    // Use basic (single-threaded) render loop on iOS to avoid threading issues
    // with Qt Multimedia VideoOutput calling UIKit APIs from render thread
    qputenv("QSG_RENDER_LOOP", "basic");
#endif

    // Install web debug logger early to capture all output
    WebDebugLogger::install();

    QApplication app(argc, argv);

    // Set application metadata
    app.setOrganizationName("DecentEspresso");
    app.setOrganizationDomain("decentespresso.com");
    app.setApplicationName("Decenza DE1");
    app.setApplicationVersion(VERSION_STRING);

    // Set Qt Quick Controls style (must be before QML engine creation)
    QQuickStyle::setStyle("Material");

    qDebug() << "App started - version" << VERSION_STRING;

    // Check for crash log from previous run (don't clear yet - QML will clear after user dismisses)
    QString previousCrashLog;
    QString previousDebugLogTail;
    if (CrashHandler::hasCrashLog()) {
        previousCrashLog = CrashHandler::readCrashLog();
        previousDebugLogTail = CrashHandler::getDebugLogTail(50);
        qWarning() << "=== PREVIOUS CRASH DETECTED ===";
        qWarning().noquote() << previousCrashLog;
        qWarning() << "=== END CRASH REPORT ===";
    }

    // Create core objects
    Settings settings;
    TranslationManager translationManager(&settings);
    BLEManager bleManager;

    // Disable BLE early in simulator mode to prevent real device connections
#if (defined(Q_OS_WIN) || defined(Q_OS_MACOS)) && defined(QT_DEBUG)
    bleManager.setDisabled(true);
#endif

    DE1Device de1Device;
    de1Device.setSettings(&settings);  // For water level auto-calibration
    std::unique_ptr<ScaleDevice> physicalScale;  // Physical BLE scale (when connected)
    FlowScale flowScale;  // Virtual scale using DE1 flow data (fallback when no BLE scale)
    ShotDataModel shotDataModel;
    MachineState machineState(&de1Device);
    machineState.setSettings(&settings);
    machineState.setScale(&flowScale);  // Start with FlowScale, switch to physical scale if found
    flowScale.setSettings(&settings);
    ProfileStorage profileStorage;
    MainController mainController(&settings, &de1Device, &machineState, &shotDataModel, &profileStorage);

    // Create and wire ShotTimingController (centralized timing and weight handling)
    ShotTimingController timingController(&de1Device);
    timingController.setScale(&flowScale);  // Start with FlowScale, switch to physical if found
    timingController.setSettings(&settings);
    timingController.setMachineState(&machineState);
    machineState.setTimingController(&timingController);
    mainController.setTimingController(&timingController);

    // Connect timing controller outputs to shot data model
    QObject::connect(&timingController, &ShotTimingController::weightSampleReady,
                     &shotDataModel, qOverload<double, double>(&ShotDataModel::addWeightSample));

    // Connect stop-at-weight signal to DE1
    QObject::connect(&timingController, &ShotTimingController::stopAtWeightReached,
                     &de1Device, &DE1Device::stopOperation);

    // Forward SAW signal to MachineState so QML shows "Target weight reached"
    QObject::connect(&timingController, &ShotTimingController::stopAtWeightReached,
                     &machineState, &MachineState::targetWeightReached);

    // Mark stop time on graph when SAW triggers
    QObject::connect(&timingController, &ShotTimingController::stopAtWeightReached,
                     [&timingController, &shotDataModel]() {
                         shotDataModel.markStopAt(timingController.shotTime());
                     });

    // Connect per-frame weight exit to DE1
    QObject::connect(&timingController, &ShotTimingController::perFrameWeightReached,
                     [&de1Device](int frameNumber) {
                         de1Device.skipToNextFrame();
                         Q_UNUSED(frameNumber);
                     });

    // Connect SAW learning signal to settings persistence
    QObject::connect(&timingController, &ShotTimingController::sawLearningComplete,
                     [&settings](double drip, double flowAtStop, double overshoot) {
                         QString scaleType = settings.scaleType();
                         settings.addSawLearningPoint(drip, flowAtStop, scaleType);
                         qDebug() << "[SAW] Learning point saved: drip=" << drip
                                  << "flow=" << flowAtStop << "overshoot=" << overshoot
                                  << "scale=" << scaleType;
                     });

    // Forward sawSettling state to MainController for QML binding
    QObject::connect(&timingController, &ShotTimingController::sawSettlingChanged,
                     &mainController, &MainController::sawSettlingChanged);

    // Connect shot ended to timing controller
    QObject::connect(&machineState, &MachineState::shotEnded,
                     &timingController, &ShotTimingController::endShot);

    // Connect shot processing to MainController (waits for SAW settling if needed)
    QObject::connect(&timingController, &ShotTimingController::shotProcessingReady,
                     &mainController, &MainController::onShotEnded);

    // Create and wire AI Manager
    AIManager aiManager(&settings);
    mainController.setAiManager(&aiManager);

    // Connect FlowScale to graph initially (will be disconnected if physical scale found)
    QObject::connect(&flowScale, &ScaleDevice::weightChanged,
                     &mainController, &MainController::onScaleWeightChanged);

    ScreensaverVideoManager screensaverManager(&settings, &profileStorage);

    // Connect screensaver manager to shot server for personal media upload
    mainController.shotServer()->setScreensaverVideoManager(&screensaverManager);

    // Connect screensaver manager to data migration client for media import
    mainController.dataMigration()->setScreensaverVideoManager(&screensaverManager);

    BatteryManager batteryManager;
    batteryManager.setDE1Device(&de1Device);
    batteryManager.setSettings(&settings);

    // Auto-wake manager for scheduled wake-ups
    AutoWakeManager autoWakeManager(&settings);
    QObject::connect(&autoWakeManager, &AutoWakeManager::wakeRequested,
                     &de1Device, &DE1Device::wakeUp);
    QObject::connect(&autoWakeManager, &AutoWakeManager::wakeRequested,
                     &mainController, &MainController::autoWakeTriggered);
    // Also wake the scale
    QObject::connect(&autoWakeManager, &AutoWakeManager::wakeRequested,
                     [&physicalScale, &bleManager, &settings]() {
        qDebug() << "AutoWakeManager: Waking scale";
        if (physicalScale && physicalScale->isConnected()) {
            physicalScale->wake();
        } else if (!settings.scaleAddress().isEmpty()) {
            // Scale disconnected - try to reconnect
            QTimer::singleShot(500, &bleManager, &BLEManager::tryDirectConnectToScale);
        }
    });
    autoWakeManager.start();

    AccessibilityManager accessibilityManager;
    accessibilityManager.setTranslationManager(&translationManager);

    // Crash reporter for sending crash reports to api.decenza.coffee
    CrashReporter crashReporter;

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
                     &de1Device, [&de1Device, &bleManager](const QBluetoothDeviceInfo& device) {
        if (!de1Device.isConnected() && !de1Device.isConnecting()) {
            // Stop scanning when we start connecting to DE1
            bleManager.stopScan();
            de1Device.connectToDevice(device);
        }
    });

    // Forward DE1 log messages to BLEManager for display in connection log
    QObject::connect(&de1Device, &DE1Device::logMessage,
                     &bleManager, &BLEManager::de1LogMessage);

    // Connect to any supported scale when discovered
    QObject::connect(&bleManager, &BLEManager::scaleDiscovered,
                     [&physicalScale, &flowScale, &machineState, &mainController, &engine, &bleManager, &settings, &flowScaleFallbackTimer, &timingController](const QBluetoothDeviceInfo& device, const QString& type) {
        // Don't connect if we already have a connected scale
        if (physicalScale && physicalScale->isConnected()) {
            return;
        }

        // Stop scanning when we start connecting to a scale
        bleManager.stopScan();

        // If we already have a scale object, check if it's the same type
        if (physicalScale) {
            // Compare types (case-insensitive) - if different, we need to create a new scale
            if (physicalScale->type().compare(type, Qt::CaseInsensitive) != 0) {
                qDebug() << "Scale type changed from" << physicalScale->type() << "to" << type << "- creating new scale";
                // IMPORTANT: Clear all references before deleting the scale to prevent dangling pointers
                machineState.setScale(&flowScale);  // Switch to FlowScale first
                timingController.setScale(&flowScale);
                bleManager.setScaleDevice(nullptr);  // Clear BLEManager's reference
                physicalScale.reset();  // Now safe to delete old scale
            } else {
                flowScaleFallbackTimer.stop();  // Stop timer - we found a scale
                // Re-wire to use physical scale
                machineState.setScale(physicalScale.get());
                timingController.setScale(physicalScale.get());
                engine.rootContext()->setContextProperty("ScaleDevice", physicalScale.get());
                physicalScale->connectToDevice(device);
                return;
            }
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
        // Use getDeviceIdentifier to handle iOS (uses UUID) vs other platforms (uses MAC address)
        settings.setScaleAddress(getDeviceIdentifier(device));
        settings.setScaleType(type);
        settings.setScaleName(device.name());

        // Switch MachineState and TimingController to use physical scale instead of FlowScale
        machineState.setScale(physicalScale.get());
        timingController.setScale(physicalScale.get());

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
                         [&physicalScale, &flowScale, &machineState, &engine, &bleManager, &mainController, &timingController]() {
            if (physicalScale && physicalScale->isConnected()) {
                // Scale connected - use physical scale
                machineState.setScale(physicalScale.get());
                timingController.setScale(physicalScale.get());
                engine.rootContext()->setContextProperty("ScaleDevice", physicalScale.get());
                // Disconnect FlowScale from graph to prevent duplicate data
                QObject::disconnect(&flowScale, &ScaleDevice::weightChanged,
                                    &mainController, &MainController::onScaleWeightChanged);
                qDebug() << "Scale connected - switched to physical scale";
            } else if (physicalScale) {
                // Scale disconnected - fall back to FlowScale
                machineState.setScale(&flowScale);
                timingController.setScale(&flowScale);
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

    // Handle disconnect request when starting a new scan
    QObject::connect(&bleManager, &BLEManager::disconnectScaleRequested,
                     [&physicalScale, &flowScale, &machineState, &engine, &mainController, &bleManager, &timingController]() {
        if (physicalScale) {
            qDebug() << "Disconnecting scale before scan";
            // Switch to FlowScale first
            machineState.setScale(&flowScale);
            timingController.setScale(&flowScale);
            engine.rootContext()->setContextProperty("ScaleDevice", &flowScale);
            // Disconnect FlowScale from graph during scan
            QObject::disconnect(&flowScale, &ScaleDevice::weightChanged,
                                &mainController, &MainController::onScaleWeightChanged);
            // Clear BLEManager's reference before deleting
            bleManager.setScaleDevice(nullptr);
            // Now reset the physical scale
            physicalScale.reset();
        }
    });

    // Load saved scale address for direct wake connection
    QString savedScaleAddr = settings.scaleAddress();
    QString savedScaleType = settings.scaleType();
    QString savedScaleName = settings.scaleName();
    if (!savedScaleAddr.isEmpty() && !savedScaleType.isEmpty()) {
        bleManager.setSavedScaleAddress(savedScaleAddr, savedScaleType, savedScaleName);
    }

    // BLE scanning is now started from QML after first-run dialog is dismissed
    // This allows the user to turn on their scale before we start scanning

    // FlowScale weight connection is handled by the fallback timer and scale disconnect logic
    // Don't connect here - only one scale should feed the graph at a time

    // Create GHC Simulator for Windows debug builds (before engine load so it can be exposed to QML)
#if (defined(Q_OS_WIN) || defined(Q_OS_MACOS)) && defined(QT_DEBUG)
    GHCSimulator ghcSimulator;
#endif

    // Expose C++ objects to QML
    QQmlContext* context = engine.rootContext();
    context->setContextProperty("Settings", &settings);
    context->setContextProperty("TranslationManager", &translationManager);
    context->setContextProperty("BLEManager", &bleManager);
    context->setContextProperty("DE1Device", &de1Device);
    context->setContextProperty("ScaleDevice", &flowScale);  // FlowScale initially, updated when physical scale connects
    context->setContextProperty("FlowScale", &flowScale);  // Always available for calibration
    context->setContextProperty("MachineState", &machineState);
    context->setContextProperty("ShotDataModel", &shotDataModel);
    context->setContextProperty("MainController", &mainController);
    context->setContextProperty("ScreensaverManager", &screensaverManager);
    context->setContextProperty("BatteryManager", &batteryManager);
    context->setContextProperty("AccessibilityManager", &accessibilityManager);
    context->setContextProperty("ProfileStorage", &profileStorage);
    context->setContextProperty("CrashReporter", &crashReporter);
    context->setContextProperty("PreviousCrashLog", previousCrashLog);
    context->setContextProperty("PreviousDebugLogTail", previousDebugLogTail);
    context->setContextProperty("AppVersion", VERSION_STRING);
    context->setContextProperty("AppVersionCode", VERSION_CODE);
#ifdef QT_DEBUG
    context->setContextProperty("IsDebugBuild", true);
#else
    context->setContextProperty("IsDebugBuild", false);
#endif

#if (defined(Q_OS_WIN) || defined(Q_OS_MACOS)) && defined(QT_DEBUG)
    // Make GHCSimulator available to main window for window sync
    context->setContextProperty("GHCSimulator", &ghcSimulator);
#endif

    // Register types for QML (use different names to avoid conflict with context properties)
    qmlRegisterUncreatableType<DE1Device>("DecenzaDE1", 1, 0, "DE1DeviceType",
        "DE1Device is created in C++");
    qmlRegisterUncreatableType<MachineState>("DecenzaDE1", 1, 0, "MachineStateType",
        "MachineState is created in C++");
    qmlRegisterUncreatableType<AIConversation>("DecenzaDE1", 1, 0, "AIConversationType",
        "AIConversation is created in C++");

    // Register pipe geometry types for 3D pipes screensaver
    qmlRegisterType<PipeCylinderGeometry>("DecenzaDE1", 1, 0, "PipeCylinderGeometry");
    qmlRegisterType<PipeElbowGeometry>("DecenzaDE1", 1, 0, "PipeElbowGeometry");
    qmlRegisterType<PipeCapGeometry>("DecenzaDE1", 1, 0, "PipeCapGeometry");
    qmlRegisterType<PipeSphereGeometry>("DecenzaDE1", 1, 0, "PipeSphereGeometry");

    // Register strange attractor renderer for attractor screensaver
    qmlRegisterType<StrangeAttractorRenderer>("DecenzaDE1", 1, 0, "StrangeAttractorRenderer");

    // Load main QML file (QTP0001 NEW policy uses /qt/qml/ prefix)
    const QUrl url(u"qrc:/qt/qml/DecenzaDE1/qml/main.qml"_s);

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
        &app, [url](QObject *obj, const QUrl &objUrl) {
            if (!obj && url == objUrl)
                QCoreApplication::exit(-1);
        }, Qt::QueuedConnection);

    engine.load(url);

    // GHC Simulator window for Windows debug builds
#if (defined(Q_OS_WIN) || defined(Q_OS_MACOS)) && defined(QT_DEBUG)
    qDebug() << "Creating DE1 Simulator and GHC window...";

    // Enable simulation mode on DE1Device - this makes it appear "connected"
    de1Device.setSimulationMode(true);

    // Create the DE1 machine simulator
    DE1Simulator de1Simulator;

    // Set simulator on DE1Device so commands are relayed to it
    de1Device.setSimulator(&de1Simulator);

    // Give it the current profile from MainController
    QObject::connect(&mainController, &MainController::currentProfileChanged, [&de1Simulator, &mainController]() {
        de1Simulator.setProfile(mainController.currentProfileObject());
    });
    // Set initial profile
    de1Simulator.setProfile(mainController.currentProfileObject());

    // Connect dose from settings (affects puck resistance simulation)
    QObject::connect(&settings, &Settings::dyeBeanWeightChanged, [&de1Simulator, &settings]() {
        de1Simulator.setDose(settings.dyeBeanWeight());
    });
    // Set initial dose
    de1Simulator.setDose(settings.dyeBeanWeight());

    // Connect grind setting (finer grind = more resistance, can choke machine)
    QObject::connect(&settings, &Settings::dyeGrinderSettingChanged, [&de1Simulator, &settings]() {
        de1Simulator.setGrindSetting(settings.dyeGrinderSetting());
    });
    // Set initial grind
    de1Simulator.setGrindSetting(settings.dyeGrinderSetting());

    // Connect simulator state changes to DE1Device (which will emit to MachineState)
    QObject::connect(&de1Simulator, &DE1Simulator::stateChanged, [&de1Simulator, &de1Device]() {
        de1Device.setSimulatedState(de1Simulator.state(), de1Simulator.subState());
    });
    QObject::connect(&de1Simulator, &DE1Simulator::subStateChanged, [&de1Simulator, &de1Device]() {
        de1Device.setSimulatedState(de1Simulator.state(), de1Simulator.subState());
    });

    // Connect simulator shot samples to DE1Device (which will emit to MainController/graphs)
    QObject::connect(&de1Simulator, &DE1Simulator::shotSampleReceived,
                     &de1Device, &DE1Device::emitSimulatedShotSample);

    // Create SimulatedScale and connect it like a real scale
    SimulatedScale simulatedScale;
    simulatedScale.simulateConnection();

    // Replace FlowScale with SimulatedScale for graph data
    QObject::disconnect(&flowScale, &ScaleDevice::weightChanged,
                        &mainController, &MainController::onScaleWeightChanged);
    QObject::connect(&simulatedScale, &ScaleDevice::weightChanged,
                     &mainController, &MainController::onScaleWeightChanged);

    // Set SimulatedScale as the active scale for MachineState
    machineState.setScale(&simulatedScale);
    context->setContextProperty("ScaleDevice", &simulatedScale);

    // Connect simulator scale weight to SimulatedScale
    QObject::connect(&de1Simulator, &DE1Simulator::scaleWeightChanged,
                     &simulatedScale, &SimulatedScale::setSimulatedWeight);

    // Configure GHC visual controller (created earlier for main window access)
    ghcSimulator.setDE1Device(&de1Device);
    ghcSimulator.setDE1Simulator(&de1Simulator);

    QQmlApplicationEngine ghcEngine;
    ghcEngine.rootContext()->setContextProperty("GHCSimulator", &ghcSimulator);
    ghcEngine.rootContext()->setContextProperty("DE1Device", &de1Device);
    ghcEngine.rootContext()->setContextProperty("DE1Simulator", &de1Simulator);
    ghcEngine.rootContext()->setContextProperty("Settings", &settings);

    QObject::connect(&ghcEngine, &QQmlApplicationEngine::objectCreated, &app,
        [](QObject *obj, const QUrl &objUrl) {
            if (!obj) {
                qWarning() << "GHC Simulator: Failed to load" << objUrl;
            } else {
                qDebug() << "GHC Simulator: Window created successfully";
            }
        }, Qt::QueuedConnection);

    const QUrl ghcUrl(u"qrc:/qt/qml/DecenzaDE1/qml/simulator/GHCSimulatorWindow.qml"_s);
    ghcEngine.load(ghcUrl);
#endif

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

    // Cross-platform lifecycle handling: manage scale when app is suspended/resumed
    // Note: DE1 is NOT put to sleep when backgrounded - users may switch apps while
    // the machine is heating up and expect it to continue (e.g., checking Visualizer)
    QObject::connect(&app, &QGuiApplication::applicationStateChanged,
                     [&physicalScale, &bleManager, &settings, &batteryManager](Qt::ApplicationState state) {
        static bool wasSuspended = false;

        if (state == Qt::ApplicationSuspended) {
            // App is being suspended (mobile) - sleep scale to save battery
            qDebug() << "App suspended - sleeping scale (DE1 stays awake)";
            wasSuspended = true;

#ifdef Q_OS_ANDROID
            // Disable accessibility bridge before surface is destroyed.
            // Prevents deadlock between QtAndroidAccessibility::runInObjectContext()
            // and QAndroidPlatformOpenGLWindow::eglSurface() that causes SIGABRT
            // when the render thread tries to swap buffers after Android destroys
            // the EGL surface while the accessibility thread holds the lock.
            QAccessible::setActive(false);
#endif

            if (physicalScale && physicalScale->isConnected()) {
                physicalScale->sleep();
                // Give BLE write time to complete before app suspends
                // de1app waits 1 second, we use 500ms as a compromise
                QEventLoop waitLoop;
                QTimer::singleShot(500, &waitLoop, &QEventLoop::quit);
                waitLoop.exec();
            }
            // DE1 intentionally NOT put to sleep - user may be checking other apps
            // while machine heats up

            // IMPORTANT: Ensure charger is ON when app goes to background
            // This prevents tablet from dying if user doesn't return to the app
            batteryManager.ensureChargerOn();
        }
        else if (state == Qt::ApplicationActive && wasSuspended) {
            // App resumed from suspended state - wake scale
            qDebug() << "App resumed - waking scale";
            wasSuspended = false;

#ifdef Q_OS_ANDROID
            // Re-enable accessibility bridge now that the EGL surface is valid again
            QAccessible::setActive(true);
#endif

            // Sync settings from disk to ensure we have latest values
            // (prevents theme colors from falling back to defaults on wake)
            settings.sync();

            // Try to reconnect/wake scale
            if (physicalScale && physicalScale->isConnected()) {
                physicalScale->wake();
            } else if (!settings.scaleAddress().isEmpty()) {
                // Scale disconnected while suspended - try to reconnect
                QTimer::singleShot(500, &bleManager, &BLEManager::tryDirectConnectToScale);
            }

            // Resume smart charging check now that app is active again
            batteryManager.checkBattery();
        }
    });

    // Remote sleep via MQTT/REST API - put scale to sleep
    QObject::connect(&mainController, &MainController::remoteSleepRequested,
                     [&physicalScale]() {
        qDebug() << "Remote sleep requested - sleeping scale";
        if (physicalScale && physicalScale->isConnected()) {
            physicalScale->sleep();
        }
    });

    // Turn off scale LCD when DE1 sleeps, wake when DE1 wakes (like de1app's decentscale_off plugin)
    // Uses disableLcd() instead of sleep() to keep BLE connected - no reconnection needed on wake
    QObject::connect(&machineState, &MachineState::phaseChanged,
                     [&physicalScale, &machineState]() {
        auto phase = machineState.phase();
        if (phase == MachineState::Phase::Sleep) {
            if (physicalScale && physicalScale->isConnected()) {
                qDebug() << "DE1 going to sleep - disabling scale LCD";
                physicalScale->disableLcd();
            }
        } else if (phase == MachineState::Phase::Idle) {
            if (physicalScale && physicalScale->isConnected()) {
                qDebug() << "DE1 woke up - waking scale LCD";
                physicalScale->wake();
            }
        }
    });

    // Cleanup on exit
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&accessibilityManager, &batteryManager, &de1Device, &physicalScale]() {
        qDebug() << "Application exiting - shutting down devices";

        bool needBleWait = false;

        // Put DE1 to sleep if connected (this is more reliable than QML onClosing on mobile)
        if (de1Device.isConnected()) {
            qDebug() << "Sending DE1 to sleep on app exit";
            de1Device.goToSleep();
            needBleWait = true;
        }

        // Put scale to sleep if connected
        if (physicalScale && physicalScale->isConnected()) {
            qDebug() << "Sending physical scale to sleep on app exit";
            physicalScale->sleep();
            needBleWait = true;
        }

        // Wait for BLE writes to complete before exiting
        // de1app waits 5-10 seconds; we use 2 seconds to ensure sleep command is sent
        if (needBleWait) {
            qDebug() << "Waiting 2s for BLE writes to complete...";
            QEventLoop waitLoop;
            QTimer::singleShot(2000, &waitLoop, &QEventLoop::quit);
            waitLoop.exec();
        }

        // IMPORTANT: Ensure charger is ON before exiting
        // This matches de1app's app_exit behavior - always leave charger ON for safety
        batteryManager.ensureChargerOn();

        // Disable Qt's accessibility bridge before window destruction
        // This prevents iOS crash (SIGBUS) where the accessibility system tries to
        // sync with already-destroyed QML items during app exit
        QAccessible::setActive(false);

        // Shutdown accessibility to stop TTS before any other cleanup
        // This prevents race conditions with Android's hwuiTask thread
        accessibilityManager.shutdown();
    });

    int result = app.exec();

    // Disable crash handler before cleanup - crashes during C++ runtime destruction
    // are not actionable and shouldn't prompt users to submit bug reports
    CrashHandler::uninstall();

    return result;
}
