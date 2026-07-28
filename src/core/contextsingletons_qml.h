#pragma once

// QML registration for objects main() owns and used to publish with setContextProperty().
//
// WHY THIS FILE EXISTS SEPARATELY FROM THE CLASS HEADERS
// -----------------------------------------------------
// The obvious move is to put QML_ELEMENT straight in each class header, the way CoffeeBagStorage
// and the types reachable through MainController do it.
//
// THE REASON THIS FILE GIVES FOR NOT DOING THAT IS OUT OF DATE. It used to say the classes here
// are compiled into test and tool targets that link no Qt6::Qml, citing add_decenza_test() as
// linking "Test/Core/Bluetooth/Sql/Network/Gui and nothing else". That is false: decenza_testlib
// links Qt6::Qml PUBLIC (tests/CMakeLists.txt, since #1617) and every add_decenza_test() target
// links decenza_testlib, so they all get it transitively — de1device.cpp, this file's headline
// example, is compiled straight into decenza_testlib. The four targets that genuinely lack
// Qt6::Qml (profile_sync, shot_eval, saw_replay, saw_parity) compile none of these classes.
//
// So the link-time argument does not currently justify the indirection. What may still justify it
// is narrower and UNMEASURED: keeping <QtQml/...> out of class headers that much of the app
// includes, so adding or changing a registration does not invalidate every TU that includes them.
// Nobody has measured that, so do not repeat it as the reason either. What IS true and worth
// keeping: this file compiles only into the Decenza target, so whatever it includes stops here.
//
// Only the Decenza target compiles this file (CMakeLists.txt), so the QtQml dependency stops
// here no matter how widely the underlying headers are included.
//
// WHY SINGLETONS RATHER THAN CONTEXT PROPERTIES
// ---------------------------------------------
// A context property is invisible to qmllint, qmlcachegen and the language server: the name
// resolves at runtime and at no other time. Every QML reference to one counts as an unqualified
// access — and, worse, is indistinguishable from a typo, because nothing in the build can tell
// the two apart. #1661 is what that costs when it goes wrong.
//
// The 30 context properties live when this file was created accounted for 943 such warnings. The
// names registered BELOW are now 757 of those 943 — 660 in the first batch (#1680), plus
// SteamHealthTracker, FlowCalibrationModel, ProfileStorage and McpServer, plus USBManager and
// UsbScaleManager. Update this figure when you add an entry; it is the one number in this file
// that goes stale silently.
//
// THREE remain, listed in openspec/changes/fix-qmllint-usability/tasks.md. ScaleDevice and
// Refractometer are re-pointed at runtime and need a forwarding façade — genuinely the hardest
// thing left, and section 4 defers it deliberately. GHCSimulator is the case described below.
//
// "Inside `#ifndef Q_OS_IOS`" used to be listed here as a blocker for USBManager and
// UsbScaleManager. It was a real obstacle but a small one, and it is gone: see
// decenzaOptionalSingleton() below. "Types already registered uncreatable in their own headers"
// was listed as a blocker too — that one was simply wrong. Only SteamHealthTracker was ever in
// that shape, and the registration just moved here.
//
// LIFETIME — READ BEFORE ADDING AN ENTRY
// --------------------------------------
// Every object registered here MUST be declared before `QQmlApplicationEngine engine`
// (src/main.cpp) so it is destroyed after it. This is not a stylistic preference. A context
// property is dropped by QML when its object emits destroyed(), so publishing a short-lived
// object that way is survivable; a singleton published through a static raw pointer has no such
// mechanism, and nothing nulls it. main.cpp's teardown comment records a refractometer crash
// from exactly this ordering — the object died first while live bindings were still reading it.
//
// So: hoist the declaration first, then register. flowCalibrationModel was hoisted for exactly
// that reason and is registered below; de1SimulatorPtr is still declared after `engine` and is
// deliberately NOT here.
//
// ghcSimulator is still NOT here, and the reason has changed. The optional-singleton path it was
// waiting for now exists (decenzaOptionalSingleton, below) and would handle its
// `#if (Q_OS_WIN || Q_OS_MACOS) && QT_DEBUG` guard exactly as it handles the USB pair's. What
// blocks it is the LIFETIME rule above: `GHCSimulator ghcSimulator` is declared in main.cpp AFTER
// `QQmlApplicationEngine engine`, so registering it as-is publishes a raw pointer to an object
// that dies while live bindings can still read it — the refractometer crash, again. Registering it
// means hoisting that declaration above the engine first, and that is a change to debug-desktop
// startup ordering for 15 warnings on a build nobody ships. Do the hoist, then register; do not
// register without the hoist.

#include <QtQml/qqmlregistration.h>
#include <QtQml/QQmlEngine>
#include <QtQml/QJSEngine>

#include "../ble/de1device.h"
#include "../screensaver/screensavervideomanager.h"
#include "../ble/blemanager.h"
#include "../core/widgetlibrary.h"
#include "../network/librarysharing.h"
#include "../weather/weathermanager.h"
#include "../core/batterymanager.h"
#include "../mcp/mcpremoteaccess.h"
#include "../models/shotdatamodel.h"
#include "../network/crashreporter.h"
#include "../models/steamdatamodel.h"
#include "../core/memorymonitor.h"
#include "../core/autowakemanager.h"
#include "../history/shothistoryexporter.h"
#include "../core/profilestorage.h"
#include "../mcp/mcpserver.h"
#include "../machine/steamhealthtracker.h"
#include "../models/flowcalibrationmodel.h"
#include "../usb/usbmanager.h"
#include "../usb/usbscalemanager.h"

// Compiled on every platform except iOS and Android — see the CMakeLists.txt block that lists
// ghcsimulator.cpp for why the condition is spelled this way and must stay in lockstep.
#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID)
#include "../simulator/ghcsimulator.h"
#endif

// Shared body of every create() below. The QML_* and Q_GADGET macros have to appear literally in
// each struct — moc does not expand preprocessor macros when it looks for them, so the structs
// cannot be generated by a macro — but the checked publish logic can be shared, and should be:
// three hand-written copies of it already exist and two of them differ in what they report.
//
// It briefly lived in its own header, src/core/qmlsingletonpublish.h, when this batch added an
// AppInfo singleton that would have been a second caller. Review deleted AppInfo (its values had
// owners already — see main.cpp), which left the extraction with one caller and no justification,
// so it came back here.
template <typename T>
T* decenzaPublishedSingleton(T* instance, QJSEngine* engine, const char* qmlName)
{
    // Checked, not asserted. QT_FORCE_ASSERTS is only defined for sanitizer builds
    // (CMakeLists.txt), so Q_ASSERT compiles out of a shipped Release — and this is the one
    // condition where that matters: with the instance unpublished, create() returns null, every
    // reference to the name reads undefined, and nothing says why.
    if (!instance) {
        qCritical("%s: QML asked for the singleton before main() published it. Every binding on "
                  "this name will be undefined. Publish it before QQmlEngine::load().", qmlName);
        return nullptr;
    }
    if (engine->thread() != instance->thread()) {
        qCritical("%s: the QML engine and the instance are on different threads; QML property "
                  "access would be unsafe.", qmlName);
        return nullptr;
    }
    QJSEngine::setObjectOwnership(instance, QJSEngine::CppOwnership);
    return instance;
}

// The same publish, for a name whose instance legitimately does not exist on some builds.
//
// decenzaPublishedSingleton() treats a null instance as always a defect, which is right for a
// name main() unconditionally owns and wrong for one behind a platform guard: USBManager and
// UsbScaleManager are declared inside `#ifndef Q_OS_IOS`, so on iOS there is nothing to publish
// and a qCritical on every launch would be noise reporting the intended state.
//
// A null return still gives QML `undefined` for the name, which is exactly what the QML side
// already expects — every reference is either behind `Qt.platform.os !== "ios"` or a
// `typeof X !== "undefined"` guard. The difference from a context property is that the name is
// now a declared type: qmllint checks the member you reach for, and a typo in it fails even
// though the instance may be absent at runtime.
//
// Do NOT reach for this to silence a name that should always be present. The loud version is the
// default for a reason — an unpublished mandatory singleton reads as "every binding is undefined"
// with nothing saying why.
template <typename T>
T* decenzaOptionalSingleton(T* instance, QJSEngine* engine, const char* qmlName)
{
    if (!instance)
        return nullptr;
    return decenzaPublishedSingleton(instance, engine, qmlName);
}

// 236 unqualified references across 26 QML files — the largest single name in the app.
// Also published to the GHC simulator window's second engine; a singleton covers both by
// construction, where the context property needed a second setContextProperty() call.
struct DE1DeviceForeign
{
    Q_GADGET
    QML_FOREIGN(DE1Device)
    QML_SINGLETON
    QML_NAMED_ELEMENT(DE1Device)

public:
    inline static DE1Device* s_singletonInstance = nullptr;
    static DE1Device* create(QQmlEngine*, QJSEngine* engine)
    {
        return decenzaPublishedSingleton(s_singletonInstance, engine, "DE1Device");
    }
};

// 179 references across 8 files. NOTE the name mismatch: the C++ class is
// ScreensaverVideoManager and QML has always called it ScreensaverManager, which is why this
// needs QML_NAMED_ELEMENT rather than QML_ELEMENT. Renaming either side is a separate change.
struct ScreensaverManagerForeign
{
    Q_GADGET
    QML_FOREIGN(ScreensaverVideoManager)
    QML_SINGLETON
    QML_NAMED_ELEMENT(ScreensaverManager)

public:
    inline static ScreensaverVideoManager* s_singletonInstance = nullptr;
    static ScreensaverVideoManager* create(QQmlEngine*, QJSEngine* engine)
    {
        return decenzaPublishedSingleton(s_singletonInstance, engine, "ScreensaverManager");
    }
};


// 76 references across 6 QML files.
struct BLEManagerForeign
{
    Q_GADGET
    QML_FOREIGN(BLEManager)
    QML_SINGLETON
    QML_NAMED_ELEMENT(BLEManager)

public:
    inline static BLEManager* s_singletonInstance = nullptr;
    static BLEManager* create(QQmlEngine*, QJSEngine* engine)
    {
        return decenzaPublishedSingleton(s_singletonInstance, engine, "BLEManager");
    }
};

// 38 references across 3 QML files.
struct WidgetLibraryForeign
{
    Q_GADGET
    QML_FOREIGN(WidgetLibrary)
    QML_SINGLETON
    QML_NAMED_ELEMENT(WidgetLibrary)

public:
    inline static WidgetLibrary* s_singletonInstance = nullptr;
    static WidgetLibrary* create(QQmlEngine*, QJSEngine* engine)
    {
        return decenzaPublishedSingleton(s_singletonInstance, engine, "WidgetLibrary");
    }
};

// 29 references across 2 QML files.
struct LibrarySharingForeign
{
    Q_GADGET
    QML_FOREIGN(LibrarySharing)
    QML_SINGLETON
    QML_NAMED_ELEMENT(LibrarySharing)

public:
    inline static LibrarySharing* s_singletonInstance = nullptr;
    static LibrarySharing* create(QQmlEngine*, QJSEngine* engine)
    {
        return decenzaPublishedSingleton(s_singletonInstance, engine, "LibrarySharing");
    }
};

// 27 references across 1 QML file.
struct WeatherManagerForeign
{
    Q_GADGET
    QML_FOREIGN(WeatherManager)
    QML_SINGLETON
    QML_NAMED_ELEMENT(WeatherManager)

public:
    inline static WeatherManager* s_singletonInstance = nullptr;
    static WeatherManager* create(QQmlEngine*, QJSEngine* engine)
    {
        return decenzaPublishedSingleton(s_singletonInstance, engine, "WeatherManager");
    }
};

// 23 references across 4 QML files.
struct BatteryManagerForeign
{
    Q_GADGET
    QML_FOREIGN(BatteryManager)
    QML_SINGLETON
    QML_NAMED_ELEMENT(BatteryManager)

public:
    inline static BatteryManager* s_singletonInstance = nullptr;
    static BatteryManager* create(QQmlEngine*, QJSEngine* engine)
    {
        return decenzaPublishedSingleton(s_singletonInstance, engine, "BatteryManager");
    }
};

// 22 references across 1 QML file. NOTE the name inversion: the C++ class is McpRemoteAccess and QML says RemoteMcpAccess.
struct RemoteMcpAccessForeign
{
    Q_GADGET
    QML_FOREIGN(McpRemoteAccess)
    QML_SINGLETON
    QML_NAMED_ELEMENT(RemoteMcpAccess)

public:
    inline static McpRemoteAccess* s_singletonInstance = nullptr;
    static McpRemoteAccess* create(QQmlEngine*, QJSEngine* engine)
    {
        return decenzaPublishedSingleton(s_singletonInstance, engine, "RemoteMcpAccess");
    }
};

// 16 references across 1 QML file.
struct ShotDataModelForeign
{
    Q_GADGET
    QML_FOREIGN(ShotDataModel)
    QML_SINGLETON
    QML_NAMED_ELEMENT(ShotDataModel)

public:
    inline static ShotDataModel* s_singletonInstance = nullptr;
    static ShotDataModel* create(QQmlEngine*, QJSEngine* engine)
    {
        return decenzaPublishedSingleton(s_singletonInstance, engine, "ShotDataModel");
    }
};

// 5 references across 1 QML file.
struct CrashReporterForeign
{
    Q_GADGET
    QML_FOREIGN(CrashReporter)
    QML_SINGLETON
    QML_NAMED_ELEMENT(CrashReporter)

public:
    inline static CrashReporter* s_singletonInstance = nullptr;
    static CrashReporter* create(QQmlEngine*, QJSEngine* engine)
    {
        return decenzaPublishedSingleton(s_singletonInstance, engine, "CrashReporter");
    }
};

// 4 references across 1 QML file.
struct SteamDataModelForeign
{
    Q_GADGET
    QML_FOREIGN(SteamDataModel)
    QML_SINGLETON
    QML_NAMED_ELEMENT(SteamDataModel)

public:
    inline static SteamDataModel* s_singletonInstance = nullptr;
    static SteamDataModel* create(QQmlEngine*, QJSEngine* engine)
    {
        return decenzaPublishedSingleton(s_singletonInstance, engine, "SteamDataModel");
    }
};

// 3 references across 1 QML file.
struct MemoryMonitorForeign
{
    Q_GADGET
    QML_FOREIGN(MemoryMonitor)
    QML_SINGLETON
    QML_NAMED_ELEMENT(MemoryMonitor)

public:
    inline static MemoryMonitor* s_singletonInstance = nullptr;
    static MemoryMonitor* create(QQmlEngine*, QJSEngine* engine)
    {
        return decenzaPublishedSingleton(s_singletonInstance, engine, "MemoryMonitor");
    }
};

// 1 reference across 1 QML file.
struct AutoWakeManagerForeign
{
    Q_GADGET
    QML_FOREIGN(AutoWakeManager)
    QML_SINGLETON
    QML_NAMED_ELEMENT(AutoWakeManager)

public:
    inline static AutoWakeManager* s_singletonInstance = nullptr;
    static AutoWakeManager* create(QQmlEngine*, QJSEngine* engine)
    {
        return decenzaPublishedSingleton(s_singletonInstance, engine, "AutoWakeManager");
    }
};

// 1 reference across 1 QML file.
struct ShotHistoryExporterForeign
{
    Q_GADGET
    QML_FOREIGN(ShotHistoryExporter)
    QML_SINGLETON
    QML_NAMED_ELEMENT(ShotHistoryExporter)

public:
    inline static ShotHistoryExporter* s_singletonInstance = nullptr;
    static ShotHistoryExporter* create(QQmlEngine*, QJSEngine* engine)
    {
        return decenzaPublishedSingleton(s_singletonInstance, engine, "ShotHistoryExporter");
    }
};

// 40 references across 2 QML files.
//
// This one carried QML_NAMED_ELEMENT(SteamHealthTrackerType) + QML_UNCREATABLE in its own header
// until now, because a context property named "SteamHealthTracker" resolves ahead of a type of the
// same name, so the type needed a second, suffixed name to avoid the collision. The singleton
// removes the collision rather than working around it: there is no context property left to shadow
// anything, and QML reads the enums straight off the singleton as
// SteamHealthTracker.EstablishingAfterReset. That is exactly what MachineState did when
// MachineStateType went away — see the qmlRegisterUncreatableType block in main.cpp.
struct SteamHealthTrackerForeign
{
    Q_GADGET
    QML_FOREIGN(SteamHealthTracker)
    QML_SINGLETON
    QML_NAMED_ELEMENT(SteamHealthTracker)

public:
    inline static SteamHealthTracker* s_singletonInstance = nullptr;
    static SteamHealthTracker* create(QQmlEngine*, QJSEngine* engine)
    {
        return decenzaPublishedSingleton(s_singletonInstance, engine, "SteamHealthTracker");
    }
};

// 37 references across 1 QML file. Its declaration in main.cpp was hoisted above `engine` to
// satisfy the lifetime rule at the top of this file; the three setters that give it its
// dependencies stay where they were, because those dependencies are constructed later.
struct FlowCalibrationModelForeign
{
    Q_GADGET
    QML_FOREIGN(FlowCalibrationModel)
    QML_SINGLETON
    QML_NAMED_ELEMENT(FlowCalibrationModel)

public:
    inline static FlowCalibrationModel* s_singletonInstance = nullptr;
    static FlowCalibrationModel* create(QQmlEngine*, QJSEngine* engine)
    {
        return decenzaPublishedSingleton(s_singletonInstance, engine, "FlowCalibrationModel");
    }
};

// 5 references across 1 QML file.
struct ProfileStorageForeign
{
    Q_GADGET
    QML_FOREIGN(ProfileStorage)
    QML_SINGLETON
    QML_NAMED_ELEMENT(ProfileStorage)

public:
    inline static ProfileStorage* s_singletonInstance = nullptr;
    static ProfileStorage* create(QQmlEngine*, QJSEngine* engine)
    {
        return decenzaPublishedSingleton(s_singletonInstance, engine, "ProfileStorage");
    }
};

// 5 references across 2 QML files.
struct McpServerForeign
{
    Q_GADGET
    QML_FOREIGN(McpServer)
    QML_SINGLETON
    QML_NAMED_ELEMENT(McpServer)

public:
    inline static McpServer* s_singletonInstance = nullptr;
    static McpServer* create(QQmlEngine*, QJSEngine* engine)
    {
        return decenzaPublishedSingleton(s_singletonInstance, engine, "McpServer");
    }
};

// USBManager and UsbScaleManager are declared inside `#ifndef Q_OS_IOS` in main.cpp, so on iOS
// there is no instance and create() returns null — hence decenzaOptionalSingleton() rather than
// the loud form. That guard was the whole reason these two stayed context properties.
//
// Registering them does NOT make the name resolve to an object on iOS, and it must not: the QML
// side guards every use (`Qt.platform.os !== "ios"`, `typeof USBManager !== "undefined"`) and
// those guards stay. What it buys is that the MEMBER is checked everywhere — `USBManager.de1Connected`
// and `UsbScaleManager.scaleConnected` are typed now, on every platform including iOS, so a
// misspelling fails the build rather than reading undefined on the one platform that has the object.

// 8 references across 1 QML file.
struct USBManagerForeign
{
    Q_GADGET
    QML_FOREIGN(USBManager)
    QML_SINGLETON
    QML_NAMED_ELEMENT(USBManager)

public:
    inline static USBManager* s_singletonInstance = nullptr;
    static USBManager* create(QQmlEngine*, QJSEngine* engine)
    {
        return decenzaOptionalSingleton(s_singletonInstance, engine, "USBManager");
    }
};

// 2 references across 2 QML files.
struct UsbScaleManagerForeign
{
    Q_GADGET
    QML_FOREIGN(UsbScaleManager)
    QML_SINGLETON
    QML_NAMED_ELEMENT(UsbScaleManager)

public:
    inline static UsbScaleManager* s_singletonInstance = nullptr;
    static UsbScaleManager* create(QQmlEngine*, QJSEngine* engine)
    {
        return decenzaOptionalSingleton(s_singletonInstance, engine, "UsbScaleManager");
    }
};

// 3 references in main.qml, plus GHCSimulatorWindow.qml which its own engine loads.
//
// The TYPE exists on every desktop platform; the INSTANCE only on a debug Windows/macOS build,
// where main.cpp declares it inside `#if (Q_OS_WIN || Q_OS_MACOS) && QT_DEBUG`. That split is
// deliberate. Registering only where the instance can exist would leave the type out of
// Decenza.qmltypes on Linux, and since the qmllint baseline is generated on macOS while the gate
// runs on Linux, the result is a ceiling CI cannot achieve.
//
// main.qml already guards on truthiness (`typeof GHCSimulator !== "undefined" && GHCSimulator`),
// which is what stays correct now that the name always resolves to a type and create() may hand
// back null. Do not "simplify" that to the typeof test alone.
#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID)
struct GHCSimulatorForeign
{
    Q_GADGET
    QML_FOREIGN(GHCSimulator)
    QML_SINGLETON
    QML_NAMED_ELEMENT(GHCSimulator)

public:
    inline static GHCSimulator* s_singletonInstance = nullptr;
    static GHCSimulator* create(QQmlEngine*, QJSEngine* engine)
    {
        return decenzaOptionalSingleton(s_singletonInstance, engine, "GHCSimulator");
    }
};
#endif
