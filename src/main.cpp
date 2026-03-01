#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QIcon>
#include <QTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QAccessible>
#include <QDebug>
#include <QOperatingSystemVersion>
#include <memory>
#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include "version.h"

#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QCoreApplication>
#endif



#include "core/asynclogger.h"
#include "core/settings.h"
#include "core/translationmanager.h"
#include "core/batterymanager.h"
#include "core/accessibilitymanager.h"
#include "core/autowakemanager.h"
#include "core/databasebackupmanager.h"
#include "core/crashhandler.h"
#include "network/crashreporter.h"
#include "core/profilestorage.h"
#include "ble/blemanager.h"
#include "ble/de1device.h"
#ifndef Q_OS_IOS
#include "usb/usbmanager.h"
#include "usb/usbscalemanager.h"
#include "usb/usbdecentscale.h"
#include "usb/serialtransport.h"
#endif
#include "ble/scaledevice.h"
#include "ble/scales/scalefactory.h"
#include "ble/scales/flowscale.h"
#include "machine/machinestate.h"
#include "machine/weightprocessor.h"
#include "models/shotdatamodel.h"
#include "controllers/maincontroller.h"
#include "controllers/shottimingcontroller.h"
#include "ai/aimanager.h"
#include "ai/aiconversation.h"
#include "screensaver/screensavervideomanager.h"
#include "screensaver/strangeattractorrenderer.h"
#include "rendering/fastlinerenderer.h"
#ifdef ENABLE_QUICK3D
#include "screensaver/pipegeometry.h"
#endif
#include "network/webdebuglogger.h"
#include "core/widgetlibrary.h"
#include "network/librarysharing.h"
#include "core/documentformatter.h"
#include "weather/weathermanager.h"
#include "models/flowcalibrationmodel.h"

// GHC Simulator for Windows debug builds
#if (defined(Q_OS_WIN) || defined(Q_OS_MACOS)) && defined(QT_DEBUG)
#include "simulator/ghcsimulator.h"
#include "simulator/de1simulator.h"
#include "simulator/simulatedscale.h"
#endif

using namespace Qt::StringLiterals;

int main(int argc, char *argv[])
{
    // Install async logger FIRST — sits at bottom of handler chain.
    // All handlers above (CrashHandler, WebDebugLogger, ShotDebugLogger) do
    // fast in-memory work, then call through to AsyncLogger which does
    // non-blocking I/O on a background thread. This eliminates synchronous
    // logcat writes (~500μs each on Android) from the main thread.
    AsyncLogger::install();

    // Install crash handler - catches SIGSEGV, SIGABRT, etc.
    CrashHandler::install();

    // Include wall clock in all log messages on all platforms
    qSetMessagePattern("[LOG] [%{time HH:mm:ss.zzz}] %{message}");

#ifdef Q_OS_IOS
    // Use basic (single-threaded) render loop on iOS to avoid threading issues
    // with Qt Multimedia VideoOutput calling UIKit APIs from render thread
    qputenv("QSG_RENDER_LOOP", "basic");
#endif

    // Install web debug logger early to capture all output
    WebDebugLogger::install();

    QApplication app(argc, argv);

#ifdef Q_OS_MACOS
    // Workaround for macOS Tahoe (26.x) beta crash in Apple Color Emoji rendering.
    // PNGReadPlugin::InitializePluginData crashes at 0x0bad4007 when CoreText tries
    // to decode color emoji bitmaps from the sbix font table via CTFontDrawGlyphs →
    // CopyEmojiImage. NativeTextRendering calls QCoreTextFontEngine::imageForGlyph
    // which triggers this path. QtTextRendering uses distance fields instead, which
    // gets glyph outlines (not bitmaps) from CoreText, completely avoiding the crash.
    // This app renders emoji as SVG images (Theme.emojiToImage), so bitmap emoji
    // glyphs are not needed.
    // Apply on macOS 16+ (Tahoe). The version may be reported as 16 or 26
    // depending on the beta build, so check >= 16 to cover both cases.
    if (QOperatingSystemVersion::current() >= QOperatingSystemVersion(QOperatingSystemVersion::MacOS, 16, 0)) {
        QQuickWindow::setTextRenderType(QQuickWindow::QtTextRendering);
        qInfo() << "macOS 16+ detected: using QtTextRendering to workaround PNGReadPlugin crash";
    }
#endif

    // Set application metadata
    app.setOrganizationName("DecentEspresso");
    app.setOrganizationDomain("decentespresso.com");
    app.setApplicationName("Decenza DE1");
    app.setApplicationVersion(VERSION_STRING);

    // Set Qt Quick Controls style (must be before QML engine creation)
    QQuickStyle::setStyle("Material");

    qDebug() << "App started - version" << VERSION_STRING;

    // Startup timing - always on, lightweight. Helps diagnose ANRs on slow devices.
    // Wall clock comes from WebDebugLogger's [LOG HH:mm:ss.zzz] prefix automatically.
    QElapsedTimer startupTimer;
    startupTimer.start();
    auto checkpoint = [&startupTimer](const char* label) {
        qDebug() << "[Startup]" << label << "-" << startupTimer.elapsed() << "ms";
    };

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
    checkpoint("Crash check done");

    // Create core objects
    Settings settings;
    checkpoint("Settings");

    // Shared QNetworkAccessManager — one I/O thread for all HTTP consumers.
    // Passed by pointer (dependency injection) to every class that needs HTTP.
    QNetworkAccessManager sharedNetworkManager;

    TranslationManager translationManager(&sharedNetworkManager, &settings);
    checkpoint("TranslationManager");
    BLEManager bleManager;

    // Disable BLE when simulation mode is active
#if (defined(Q_OS_WIN) || defined(Q_OS_MACOS)) && defined(QT_DEBUG)
    bleManager.setDisabled(settings.simulationMode());
#endif

    DE1Device de1Device;
    de1Device.setSettings(&settings);  // For water level auto-calibration
    de1Device.setSimulationMode(settings.simulationMode());  // Restore simulation mode from settings
    std::unique_ptr<ScaleDevice> physicalScale;  // Physical BLE scale (when connected)
    FlowScale flowScale;  // Virtual scale using DE1 flow data (fallback when no BLE scale)
    ShotDataModel shotDataModel;
    MachineState machineState(&de1Device);
    machineState.setSettings(&settings);
    machineState.setScale(&flowScale);  // Start with FlowScale, switch to physical scale if found
    flowScale.setSettings(&settings);
    ProfileStorage profileStorage;
#ifndef Q_OS_IOS
    USBManager usbManager;
    UsbScaleManager usbScaleManager;
#endif
    checkpoint("Core objects");
    MainController mainController(&sharedNetworkManager, &settings, &de1Device, &machineState, &shotDataModel, &profileStorage);
    checkpoint("MainController");

    // Create and wire ShotTimingController (centralized timing and weight handling)
    ShotTimingController timingController(&de1Device);
    timingController.setScale(&flowScale);  // Start with FlowScale, switch to physical if found
    timingController.setSettings(&settings);
    timingController.setMachineState(&machineState);
    machineState.setTimingController(&timingController);
    mainController.setTimingController(&timingController);
    mainController.setBLEManager(&bleManager);
    mainController.setFlowScale(&flowScale);

    // Connect timing controller outputs to shot data model
    QObject::connect(&timingController, &ShotTimingController::weightSampleReady,
                     &shotDataModel, qOverload<double, double, double>(&ShotDataModel::addWeightSample));

    // Batch shotTimeChanged onto the 33ms flush timer (signal-to-signal connection)
    // This avoids expensive QML binding evaluation in the BLE signal handler
    QObject::connect(&shotDataModel, &ShotDataModel::flushed,
                     &timingController, &ShotTimingController::shotTimeChanged);

    // SAW stop, per-frame weight exit, and graph markings are now handled by
    // WeightProcessor signals (stopNow, skipFrame) wired below.
    // ShotTimingController::stopAtWeightReached and perFrameWeightReached are no longer emitted.

    // Connect SAW learning signal to settings persistence
    QObject::connect(&timingController, &ShotTimingController::sawLearningComplete,
                     [&settings](double drip, double flowAtStop, double overshoot) {
                         QString scaleType = settings.scaleType();
                         settings.addSawLearningPoint(drip, flowAtStop, scaleType, overshoot);
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

    checkpoint("ShotTimingController wiring");

    // Weight processor on dedicated worker thread — isolates LSLR + SOW decisions
    // from main thread stalls (GC pauses, remaining synchronous I/O).
    WeightProcessor weightProcessor;
    QThread weightThread;
    weightThread.setObjectName(QStringLiteral("WeightProcessor"));
    weightProcessor.moveToThread(&weightThread);
    weightThread.start(QThread::HighPriority);

    // Scale → WeightProcessor (main → worker, auto QueuedConnection)
    // Initially connected to FlowScale; reconnected when physical scale is found
    QObject::connect(&flowScale, &ScaleDevice::weightChanged,
                     &weightProcessor, &WeightProcessor::processWeight);

    // WeightProcessor → DE1Device: stop-at-weight (auto QueuedConnection back to main thread)
    QObject::connect(&weightProcessor, &WeightProcessor::stopNow,
                     &de1Device, qOverload<qint64>(&DE1Device::stopOperationUrgent));

    // WeightProcessor → MachineState: forward SAW trigger for QML "Target reached" display
    QObject::connect(&weightProcessor, &WeightProcessor::stopNow,
                     &machineState, [&machineState](qint64) {
                         emit machineState.targetWeightReached();
                     });

    // WeightProcessor → ShotDataModel: mark stop time on graph.
    // Using &shotDataModel as context ensures lambda runs on the main thread.
    QObject::connect(&weightProcessor, &WeightProcessor::stopNow,
                     &shotDataModel, [&timingController, &shotDataModel](qint64) {
                         shotDataModel.markStopAt(timingController.shotTime());
                     });

    // WeightProcessor → DE1Device: per-frame weight exit.
    // Using &de1Device as context ensures BLE write happens on the main thread.
    QObject::connect(&weightProcessor, &WeightProcessor::skipFrame,
                     &de1Device, [&de1Device](int) { de1Device.skipToNextFrame(); });

    // WeightProcessor → ShotTimingController: SAW learning context
    QObject::connect(&weightProcessor, &WeightProcessor::sawTriggered,
                     &timingController, &ShotTimingController::onSawTriggered);

    // WeightProcessor → ShotTimingController: record weight exits for transition tracking
    QObject::connect(&weightProcessor, &WeightProcessor::skipFrame,
                     &timingController, &ShotTimingController::recordWeightExit);

    // WeightProcessor → ShotTimingController: flow rates for graph and settling
    QObject::connect(&weightProcessor, &WeightProcessor::flowRatesReady,
                     &timingController, &ShotTimingController::onWeightSample);

    // WeightProcessor → MachineState: cached flow rate for QML property.
    // Using &machineState as context ensures lambda runs on the main thread.
    QObject::connect(&weightProcessor, &WeightProcessor::flowRatesReady,
                     &machineState, [&machineState](double, double flowRate, double flowRateShort) {
                         machineState.updateCachedFlowRates(flowRate, flowRateShort);
                     });

    // Forward frame number updates from shot samples to worker thread.
    // With &weightProcessor as context, Qt auto-uses QueuedConnection (cross-thread).
    QObject::connect(&timingController, &ShotTimingController::sampleReady,
                     &weightProcessor, [&weightProcessor](double, double, double, double,
                         double, double, double, int frameNumber, bool) {
                         weightProcessor.setCurrentFrame(frameNumber);
                     });

    // Shot lifecycle → WeightProcessor: configure at shot start, stop at shot end.
    // IMPORTANT: MainController::onEspressoCycleStarted runs BEFORE this lambda
    // (connected earlier in MainController's constructor) and calls tare() synchronously.
    // So by the time this lambda runs, isTareComplete() is already true.
    // We include setTareComplete(true) in the SAME queued invocation as startExtraction()
    // to guarantee correct ordering on the worker thread. A separate tareCompleteChanged
    // connection would race: its queued setTareComplete(true) arrives on the worker BEFORE
    // startExtraction() (which resets m_tareComplete=false), causing tare to be lost.
    QObject::connect(&machineState, &MachineState::espressoCycleStarted,
                     [&weightProcessor, &machineState, &settings, &mainController, &timingController]() {
                         // Build snapshot of learning data and configuration
                         double targetWeight = machineState.targetWeight();
                         QString scaleType = settings.scaleType();
                         bool converged = settings.isSawConverged(scaleType);
                         int maxEntries = converged ? 12 : 8;
                         auto entries = settings.sawLearningEntries(scaleType, maxEntries);
                         QVector<double> drips, flows;
                         drips.reserve(entries.size());
                         flows.reserve(entries.size());
                         for (const auto& e : entries) {
                             drips.append(e.first);
                             flows.append(e.second);
                         }

                         // Build frame exit weights from current profile
                         QVector<double> frameExitWeights;
                         const Profile& profile = mainController.currentProfile();
                         {
                             const auto& steps = profile.steps();
                             frameExitWeights.reserve(steps.size());
                             for (const auto& step : steps) {
                                 frameExitWeights.append(step.exitWeight);
                             }
                         }

                         // Tare already happened synchronously in onEspressoCycleStarted
                         bool tareComplete = timingController.isTareComplete();

                         QMetaObject::invokeMethod(&weightProcessor,
                             [&weightProcessor, targetWeight, frameExitWeights, drips, flows, converged, tareComplete]() {
                                 weightProcessor.configure(targetWeight, frameExitWeights, drips, flows, converged);
                                 weightProcessor.startExtraction();
                                 if (tareComplete) {
                                     weightProcessor.setTareComplete(true);
                                 }
                             }, Qt::QueuedConnection);
                     });

    // Auto-tare during "flow before" phase → WeightProcessor: clear stale cup-weight data.
    // NOTE: resetForRetare() must NOT call setTareComplete() — see ordering comment above
    // (lines 284-287). A separate queued setTareComplete would race with startExtraction().
    QObject::connect(&machineState, &MachineState::flowBeforeAutoTare,
                     [&weightProcessor]() {
                         QMetaObject::invokeMethod(&weightProcessor, [&weightProcessor]() {
                             weightProcessor.resetForRetare();
                         }, Qt::QueuedConnection);
                     });

    QObject::connect(&machineState, &MachineState::shotEnded,
                     [&weightProcessor]() {
                         QMetaObject::invokeMethod(&weightProcessor, [&weightProcessor]() {
                             weightProcessor.stopExtraction();
                         }, Qt::QueuedConnection);
                     });

    checkpoint("WeightProcessor wiring");

    // Create and wire AI Manager
    AIManager aiManager(&sharedNetworkManager, &settings);
    mainController.setAiManager(&aiManager);

    // Connect FlowScale to graph initially (will be disconnected if physical scale found)
    QObject::connect(&flowScale, &ScaleDevice::weightChanged,
                     &mainController, &MainController::onScaleWeightChanged);

    ScreensaverVideoManager screensaverManager(&sharedNetworkManager, &settings, &profileStorage);
    checkpoint("ScreensaverVideoManager");

    // Connect screensaver manager and AI manager to shot server
    mainController.shotServer()->setScreensaverVideoManager(&screensaverManager);
    mainController.shotServer()->setAIManager(&aiManager);
    mainController.shotServer()->setMqttClient(mainController.mqttClient());
    // Connect screensaver manager to data migration client for media import
    mainController.dataMigration()->setScreensaverVideoManager(&screensaverManager);

    BatteryManager batteryManager;
    batteryManager.setDE1Device(&de1Device);
    batteryManager.setSettings(&settings);

    // Widget library for saving/sharing layout items, zones, and layouts
    WidgetLibrary widgetLibrary(&settings);

    // Library sharing - upload/download widgets to/from decenza.coffee
    LibrarySharing librarySharing(&settings, &widgetLibrary);

    // Connect widget library and sharing to shot server for web layout editor
    mainController.shotServer()->setWidgetLibrary(&widgetLibrary);
    mainController.shotServer()->setLibrarySharing(&librarySharing);

    // Weather forecast manager (hourly updates, region-aware API selection)
    WeatherManager weatherManager(&sharedNetworkManager);
    weatherManager.setLocationProvider(mainController.locationProvider());

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

    // Database backup manager for scheduled daily backups
    DatabaseBackupManager backupManager(&settings, mainController.shotHistory(),
                                       &profileStorage, &screensaverManager);
    mainController.setBackupManager(&backupManager);
    QObject::connect(&backupManager, &DatabaseBackupManager::backupCreated,
                     [](const QString& path) {
        qDebug() << "DatabaseBackupManager: Backup created successfully:" << path;
    });
    QObject::connect(&backupManager, &DatabaseBackupManager::backupFailed,
                     [](const QString& error) {
        qWarning() << "DatabaseBackupManager: Backup failed:" << error;
    });
    QObject::connect(&backupManager, &DatabaseBackupManager::profilesRestored,
                     &mainController, &MainController::refreshProfiles);
    QObject::connect(&backupManager, &DatabaseBackupManager::mediaRestored,
                     &screensaverManager, &ScreensaverVideoManager::reloadPersonalMedia);
    backupManager.start();

    checkpoint("Managers wired");

#ifndef Q_OS_IOS
    usbManager.startPolling();
    usbScaleManager.startPolling();
#endif

    AccessibilityManager accessibilityManager;
    accessibilityManager.setTranslationManager(&translationManager);

    // Crash reporter for sending crash reports to api.decenza.coffee
    CrashReporter crashReporter;

    checkpoint("Pre-QML setup done");

    // Set up QML engine
    QQmlApplicationEngine engine;
    checkpoint("QML engine created");

    // Auto-connect when DE1 is discovered via BLE
    QObject::connect(&bleManager, &BLEManager::de1Discovered,
                     &de1Device, [&de1Device, &bleManager, &physicalScale
#ifndef Q_OS_IOS
                     , &usbManager
#endif
                     ](const QBluetoothDeviceInfo& device) {
#ifndef Q_OS_IOS
        // Don't connect via BLE if already connected via USB
        if (usbManager.isDe1Connected()) {
            return;
        }
#endif
        if (!de1Device.isConnected() && !de1Device.isConnecting()) {
            de1Device.connectToDevice(device);
            // Only stop scan if we're not still looking for a scale
            bool lookingForScale = bleManager.hasSavedScale() || bleManager.isScanningForScales();
            if (!lookingForScale || (physicalScale && physicalScale->isConnected())) {
                bleManager.stopScan();
            }
        }
    });

    // Forward DE1 log messages to BLEManager for display in connection log
    QObject::connect(&de1Device, &DE1Device::logMessage,
                     &bleManager, &BLEManager::de1LogMessage);

#ifndef Q_OS_IOS
    // When USB DE1 discovered: disconnect BLE, switch to USB transport
    QObject::connect(&usbManager, &USBManager::de1Discovered,
        [&de1Device, &bleManager](SerialTransport* transport) {
            // Disconnect BLE if connected
            if (de1Device.isConnected()) {
                de1Device.disconnect();
            }
            // Stop BLE scanning while USB is connected
            bleManager.stopScan();
            // Switch to USB transport
            de1Device.setTransport(transport);
        });

    // When USB DE1 lost: clear transport, BLE scanning can resume
    QObject::connect(&usbManager, &USBManager::de1Lost,
        [&de1Device, &bleManager]() {
            de1Device.disconnect();
            // Resume BLE scanning to find DE1 via Bluetooth
            bleManager.startScan();
        });

    // Forward USBManager log messages to BLEManager for display in connection log
    QObject::connect(&usbManager, &USBManager::logMessage,
                     &bleManager, &BLEManager::de1LogMessage);
#endif

    // Connect to any supported scale when discovered
    QObject::connect(&bleManager, &BLEManager::scaleDiscovered,
                     [&physicalScale, &flowScale, &machineState, &mainController, &engine, &bleManager, &settings, &timingController, &de1Device, &weightProcessor](const QBluetoothDeviceInfo& device, const QString& type) {
        // Don't connect if we already have a connected scale
        if (physicalScale && physicalScale->isConnected()) {
            return;
        }

        // Only stop scan if DE1 is already connected/connecting
        if (de1Device.isConnected() || de1Device.isConnecting()) {
            bleManager.stopScan();
        }

        // If we already have a scale object, check if it's the same type
        if (physicalScale) {
            // Compare types (case-insensitive) - if different, we need to create a new scale
            if (physicalScale->type().compare(type, Qt::CaseInsensitive) != 0) {
                qDebug() << "Scale type changed from" << physicalScale->type() << "to" << type << "- creating new scale";
                // IMPORTANT: Clear all references before deleting the scale to prevent dangling pointers
                machineState.setScale(&flowScale);  // Switch to FlowScale first
                timingController.setScale(&flowScale);
                // Reconnect FlowScale to WeightProcessor temporarily
                QObject::connect(&flowScale, &ScaleDevice::weightChanged,
                                 &weightProcessor, &WeightProcessor::processWeight);
                bleManager.setScaleDevice(nullptr);  // Clear BLEManager's reference
                physicalScale.reset();  // Now safe to delete old scale
            } else {
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

        // Disconnect FlowScale from graph and weight processor
        QObject::disconnect(&flowScale, &ScaleDevice::weightChanged,
                            &mainController, &MainController::onScaleWeightChanged);
        QObject::disconnect(&flowScale, &ScaleDevice::weightChanged,
                            &weightProcessor, &WeightProcessor::processWeight);

        // Connect physical scale weight updates to MainController (permanent for scale lifetime).
        // WeightProcessor connection is managed by the connectedChanged lambda below
        // to avoid double-connecting (once here + once on connect event).
        QObject::connect(physicalScale.get(), &ScaleDevice::weightChanged,
                         &mainController, &MainController::onScaleWeightChanged);

        // When physical scale connects/disconnects, switch between physical and FlowScale
        QObject::connect(physicalScale.get(), &ScaleDevice::connectedChanged,
                         [&physicalScale, &flowScale, &machineState, &engine, &bleManager, &mainController, &timingController, &weightProcessor]() {
            if (physicalScale && physicalScale->isConnected()) {
                // Scale connected - use physical scale
                machineState.setScale(physicalScale.get());
                timingController.setScale(physicalScale.get());
                engine.rootContext()->setContextProperty("ScaleDevice", physicalScale.get());
                // Disconnect FlowScale from graph and weight processor
                QObject::disconnect(&flowScale, &ScaleDevice::weightChanged,
                                    &mainController, &MainController::onScaleWeightChanged);
                QObject::disconnect(&flowScale, &ScaleDevice::weightChanged,
                                    &weightProcessor, &WeightProcessor::processWeight);
                // Connect physical scale to weight processor
                QObject::connect(physicalScale.get(), &ScaleDevice::weightChanged,
                                 &weightProcessor, &WeightProcessor::processWeight);
                // Notify MQTT
                if (mainController.mqttClient()) {
                    mainController.mqttClient()->onScaleConnectedChanged(true);
                }
                qDebug() << "Scale connected - switched to physical scale";
            } else if (physicalScale) {
                // Scale disconnected - fall back to FlowScale
                machineState.setScale(&flowScale);
                timingController.setScale(&flowScale);
                engine.rootContext()->setContextProperty("ScaleDevice", &flowScale);
                // Disconnect physical scale from weight processor
                QObject::disconnect(physicalScale.get(), &ScaleDevice::weightChanged,
                                    &weightProcessor, &WeightProcessor::processWeight);
                // Reconnect FlowScale to graph and weight processor
                QObject::connect(&flowScale, &ScaleDevice::weightChanged,
                                 &mainController, &MainController::onScaleWeightChanged);
                QObject::connect(&flowScale, &ScaleDevice::weightChanged,
                                 &weightProcessor, &WeightProcessor::processWeight);
                // Notify MQTT
                if (mainController.mqttClient()) {
                    mainController.mqttClient()->onScaleConnectedChanged(false);
                }
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
                     [&physicalScale, &flowScale, &machineState, &engine, &mainController, &bleManager, &timingController, &weightProcessor]() {
        if (physicalScale) {
            qDebug() << "Disconnecting scale before scan";
            // Switch to FlowScale first
            machineState.setScale(&flowScale);
            timingController.setScale(&flowScale);
            engine.rootContext()->setContextProperty("ScaleDevice", &flowScale);
            // Reconnect FlowScale to graph and weight processor (physical scale is being destroyed)
            QObject::connect(&flowScale, &ScaleDevice::weightChanged,
                             &mainController, &MainController::onScaleWeightChanged);
            QObject::connect(&flowScale, &ScaleDevice::weightChanged,
                             &weightProcessor, &WeightProcessor::processWeight);
            // Notify MQTT that scale is disconnected
            if (mainController.mqttClient()) {
                mainController.mqttClient()->onScaleConnectedChanged(false);
            }
            // Clear BLEManager's reference before deleting
            bleManager.setScaleDevice(nullptr);
            // Now reset the physical scale
            physicalScale.reset();
        }
    });

#ifndef Q_OS_IOS
    // When USB scale discovered: wire it as the active scale (same pattern as BLE scale)
    QObject::connect(&usbScaleManager, &UsbScaleManager::scaleDiscovered,
                     [&physicalScale, &flowScale, &machineState, &mainController, &engine,
                      &bleManager, &timingController, &weightProcessor, &usbScaleManager, &settings](UsbDecentScale* usbScale) {
        // Don't connect if we already have a connected BLE scale
        if (physicalScale && physicalScale->isConnected()) {
            qDebug() << "[USB Scale] BLE scale already connected, ignoring USB scale";
            return;
        }

        // If we have a disconnected BLE scale, clean it up
        if (physicalScale) {
            machineState.setScale(&flowScale);
            timingController.setScale(&flowScale);
            bleManager.setScaleDevice(nullptr);
            physicalScale.reset();
        }

        // Switch to USB scale
        machineState.setScale(usbScale);
        timingController.setScale(usbScale);
        engine.rootContext()->setContextProperty("ScaleDevice", usbScale);

        // Disconnect FlowScale from graph and weight processor
        QObject::disconnect(&flowScale, &ScaleDevice::weightChanged,
                            &mainController, &MainController::onScaleWeightChanged);
        QObject::disconnect(&flowScale, &ScaleDevice::weightChanged,
                            &weightProcessor, &WeightProcessor::processWeight);

        // Connect USB scale weight updates
        QObject::connect(usbScale, &ScaleDevice::weightChanged,
                         &mainController, &MainController::onScaleWeightChanged);
        QObject::connect(usbScale, &ScaleDevice::weightChanged,
                         &weightProcessor, &WeightProcessor::processWeight);

        // Save USB scale as the active scale type (so Settings panel shows it)
        settings.setScaleType(usbScale->name());
        settings.setScaleName(usbScale->name());

        // Notify MQTT
        if (mainController.mqttClient()) {
            mainController.mqttClient()->onScaleConnectedChanged(true);
        }

        qDebug() << "[USB Scale] Switched to USB scale:" << usbScale->name();
    });

    // When USB scale lost: fall back to FlowScale (or BLE scale if available)
    QObject::connect(&usbScaleManager, &UsbScaleManager::scaleLost,
                     [&physicalScale, &flowScale, &machineState, &mainController, &engine,
                      &timingController, &weightProcessor, &usbScaleManager]() {
        // Disconnect the USB scale's weight signals
        if (usbScaleManager.scale()) {
            QObject::disconnect(usbScaleManager.scale(), &ScaleDevice::weightChanged,
                                &mainController, &MainController::onScaleWeightChanged);
            QObject::disconnect(usbScaleManager.scale(), &ScaleDevice::weightChanged,
                                &weightProcessor, &WeightProcessor::processWeight);
        }

        // Fall back to BLE scale if connected, otherwise FlowScale
        if (physicalScale && physicalScale->isConnected()) {
            machineState.setScale(physicalScale.get());
            timingController.setScale(physicalScale.get());
            engine.rootContext()->setContextProperty("ScaleDevice", physicalScale.get());
            qDebug() << "[USB Scale] Lost — falling back to BLE scale";
        } else {
            machineState.setScale(&flowScale);
            timingController.setScale(&flowScale);
            engine.rootContext()->setContextProperty("ScaleDevice", &flowScale);
            // Reconnect FlowScale
            QObject::connect(&flowScale, &ScaleDevice::weightChanged,
                             &mainController, &MainController::onScaleWeightChanged);
            QObject::connect(&flowScale, &ScaleDevice::weightChanged,
                             &weightProcessor, &WeightProcessor::processWeight);
            qDebug() << "[USB Scale] Lost — falling back to FlowScale";
        }

        // Notify MQTT
        if (mainController.mqttClient()) {
            mainController.mqttClient()->onScaleConnectedChanged(false);
        }
    });

    // Forward USB scale manager log messages
    QObject::connect(&usbScaleManager, &UsbScaleManager::logMessage,
                     &bleManager, &BLEManager::de1LogMessage);
#endif // !Q_OS_IOS

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
    context->setContextProperty("FlowScale", &flowScale);  // Always available for diagnostics
    context->setContextProperty("MachineState", &machineState);
    context->setContextProperty("ShotDataModel", &shotDataModel);
    context->setContextProperty("MainController", &mainController);
    context->setContextProperty("ScreensaverManager", &screensaverManager);
    context->setContextProperty("BatteryManager", &batteryManager);
    context->setContextProperty("AccessibilityManager", &accessibilityManager);
    context->setContextProperty("ProfileStorage", &profileStorage);
    context->setContextProperty("WeatherManager", &weatherManager);
    context->setContextProperty("CrashReporter", &crashReporter);
    context->setContextProperty("WidgetLibrary", &widgetLibrary);
    context->setContextProperty("LibrarySharing", &librarySharing);
#ifndef Q_OS_IOS
    context->setContextProperty("USBManager", &usbManager);
    context->setContextProperty("UsbScaleManager", &usbScaleManager);
#endif

    FlowCalibrationModel flowCalibrationModel;
    flowCalibrationModel.setStorage(mainController.shotHistory());
    flowCalibrationModel.setSettings(&settings);
    flowCalibrationModel.setDevice(&de1Device);
    context->setContextProperty("FlowCalibrationModel", &flowCalibrationModel);

    context->setContextProperty("PreviousCrashLog", previousCrashLog);
    context->setContextProperty("PreviousDebugLogTail", previousDebugLogTail);
    context->setContextProperty("AppVersion", VERSION_STRING);
    context->setContextProperty("AppVersionCode", versionCode());
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

    // Register strange attractor renderer (QQuickPaintedItem, no Quick3D dependency)
    qmlRegisterType<StrangeAttractorRenderer>("DecenzaDE1", 1, 0, "StrangeAttractorRenderer");

    // Register fast line renderer for shot graph (QSGGeometryNode, pre-allocated VBO)
    qmlRegisterType<FastLineRenderer>("DecenzaDE1", 1, 0, "FastLineRenderer");

#ifdef ENABLE_QUICK3D
    // Register pipe geometry types for 3D pipes screensaver
    qmlRegisterType<PipeCylinderGeometry>("DecenzaDE1", 1, 0, "PipeCylinderGeometry");
    qmlRegisterType<PipeElbowGeometry>("DecenzaDE1", 1, 0, "PipeElbowGeometry");
    qmlRegisterType<PipeCapGeometry>("DecenzaDE1", 1, 0, "PipeCapGeometry");
    qmlRegisterType<PipeSphereGeometry>("DecenzaDE1", 1, 0, "PipeSphereGeometry");
#endif

    // Register DocumentFormatter for rich text editing in layout editor
    qmlRegisterType<DocumentFormatter>("DecenzaDE1", 1, 0, "DocumentFormatter");

    checkpoint("Context properties & type registration");

    // Load main QML file (QTP0001 NEW policy uses /qt/qml/ prefix)
    const QUrl url(u"qrc:/qt/qml/DecenzaDE1/qml/main.qml"_s);

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
        &app, [url, &checkpoint](QObject *obj, const QUrl &objUrl) {
            if (!obj && url == objUrl)
                QCoreApplication::exit(-1);
            else if (obj)
                checkpoint("QML objectCreated");
        }, Qt::QueuedConnection);

    engine.load(url);
    checkpoint("engine.load(main.qml) returned");

    // GHC Simulator window for debug builds (runs when simulation mode is on)
    // NOTE: These must be declared outside the if-block so they survive through
    // app.exec(). Otherwise the if-block scope destroys them before the event
    // loop starts, and signal connections become dangling references (use-after-free).
#if (defined(Q_OS_WIN) || defined(Q_OS_MACOS)) && defined(QT_DEBUG)
    std::unique_ptr<DE1Simulator> de1SimulatorPtr;
    std::unique_ptr<SimulatedScale> simulatedScalePtr;
    std::unique_ptr<QQmlApplicationEngine> ghcEnginePtr;

    if (settings.simulationMode()) {
        qDebug() << "Creating DE1 Simulator and GHC window...";

        // Create the DE1 machine simulator
        de1SimulatorPtr = std::make_unique<DE1Simulator>();
        auto& de1Simulator = *de1SimulatorPtr;

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
        simulatedScalePtr = std::make_unique<SimulatedScale>();
        auto& simulatedScale = *simulatedScalePtr;
        simulatedScale.simulateConnection();

        // Replace FlowScale with SimulatedScale for graph data
        QObject::disconnect(&flowScale, &ScaleDevice::weightChanged,
                            &mainController, &MainController::onScaleWeightChanged);
        QObject::connect(&simulatedScale, &ScaleDevice::weightChanged,
                         &mainController, &MainController::onScaleWeightChanged);

        // Set SimulatedScale as the active scale (matching physical scale pattern)
        machineState.setScale(&simulatedScale);
        timingController.setScale(&simulatedScale);
        context->setContextProperty("ScaleDevice", &simulatedScale);

        // Reconnect WeightProcessor from FlowScale to SimulatedScale for espresso SOW
        QObject::disconnect(&flowScale, &ScaleDevice::weightChanged,
                            &weightProcessor, &WeightProcessor::processWeight);
        QObject::connect(&simulatedScale, &ScaleDevice::weightChanged,
                         &weightProcessor, &WeightProcessor::processWeight);

        // Connect simulator scale weight to SimulatedScale
        QObject::connect(&de1Simulator, &DE1Simulator::scaleWeightChanged,
                         &simulatedScale, &SimulatedScale::setSimulatedWeight);

        // Configure GHC visual controller (created earlier for main window access)
        ghcSimulator.setDE1Device(&de1Device);
        ghcSimulator.setDE1Simulator(&de1Simulator);

        ghcEnginePtr = std::make_unique<QQmlApplicationEngine>();
        auto& ghcEngine = *ghcEnginePtr;
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
    }
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

    // Sync launcher alias with persisted setting (APK updates reset component states)
    settings.setLauncherMode(settings.launcherMode());
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
#ifdef Q_OS_IOS
            // On iOS, skip queued BLE writes during suspension - CoreBluetooth invalidates
            // its internal handles during app suspension, causing SIGSEGV when the queued
            // write executes 50ms later. The DE1's 10-minute auto-charger timeout provides
            // safety (it automatically re-enables the charger if no command is received).
            qDebug() << "BatteryManager: Skipping ensureChargerOn on iOS (CoreBluetooth suspension)";
#else
            batteryManager.ensureChargerOn();
#endif
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
    // de1EverAwake: suppress Sleep reaction on initial connect (DE1's default state is Sleep,
    // so MachineState transitions Disconnected→Sleep before the real state arrives)
    bool de1EverAwake = false;
    QObject::connect(&machineState, &MachineState::phaseChanged,
                     [&physicalScale, &machineState, &de1EverAwake]() {
        auto phase = machineState.phase();
        if (phase == MachineState::Phase::Disconnected) {
            de1EverAwake = false;
        } else if (phase == MachineState::Phase::Sleep) {
            if (de1EverAwake && physicalScale && physicalScale->isConnected()) {
                qDebug() << "DE1 going to sleep - disabling scale LCD";
                physicalScale->disableLcd();
            }
        } else if (phase == MachineState::Phase::Idle) {
            if (physicalScale && physicalScale->isConnected()) {
                qDebug() << "DE1 woke up - waking scale LCD";
                physicalScale->wake();
            }
            de1EverAwake = true;
        } else {
            de1EverAwake = true;
        }
    });

    // Cleanup on exit
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&accessibilityManager, &batteryManager, &de1Device, &physicalScale, &engine, &weightThread]() {
        qDebug() << "Application exiting - shutting down devices";

        // Set QML shuttingDown flag to prevent screensaver from activating.
        // Qt.quit() does NOT trigger ApplicationWindow.onClosing, so the QML-side
        // shuttingDown flag may not be set. Setting it here covers all exit paths.
        if (!engine.rootObjects().isEmpty()) {
            engine.rootObjects().first()->setProperty("shuttingDown", true);
        }

        // Stop weight processor thread first (before BLE shutdown).
        // Any pending SOW commands are no longer needed since we're exiting.
        weightThread.quit();
        weightThread.wait(1000);

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

        // Note: No need to null context properties here. All C++ objects are
        // stack-allocated before the QML engine, so reverse destruction order
        // guarantees the engine (and all QML items) is destroyed first.

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

    // Drain remaining log messages and restore default handler.
    // Must be after CrashHandler (reverse of installation order).
    AsyncLogger::uninstall();

    return result;
}
