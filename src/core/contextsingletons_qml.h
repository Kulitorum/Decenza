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
// names registered BELOW are now ALL 943 — 660 in the first batch (#1680), plus
// SteamHealthTracker, FlowCalibrationModel, ProfileStorage and McpServer, plus USBManager and
// UsbScaleManager (everywhere but iOS — see below), and finally GHCSimulator, ScaleDevice and
// Refractometer. The figure is unchanged by the iOS exclusion, because the gate that counts runs
// on Linux, where both are registered. Update this figure
// when you add an entry; it is the one number in this file that goes stale silently.
//
// NONE remain. `setContextProperty()` has no live call in main.cpp. The three that were listed
// here as remaining — ScaleDevice and Refractometer (re-pointed at runtime) and GHCSimulator
// (declared after the engine) — all landed; see openspec/changes/fix-qmllint-usability/tasks.md
// §4 and the LIFETIME note below for how each obstacle was actually removed.
//
// "Inside `#ifndef Q_OS_IOS`" used to be listed here as a blocker for USBManager and
// UsbScaleManager, and it is still one — for the TYPE. On iOS these two are not registered at all,
// because naming them means including headers that include <QSerialPort>, which that platform does
// not build or link. What #1687 did remove is the blocker for a name whose INSTANCE is optional;
// that is decenzaOptionalSingleton() below, and GHCSimulator is what it covers. See the note above
// USBManagerForeign.
// "Types already registered uncreatable in their own headers"
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
// ghcSimulator IS here now, and the hoist is what made it possible. It used to be declared in
// main.cpp after `QQmlApplicationEngine engine`, so registering it as-is would have published a
// raw pointer to an object that dies while live bindings can still read it — the refractometer
// crash, again. The declaration moved above the engine first; only then was it registered. Same
// for scaleProxy and refractometerProxy. Do the hoist, then register; do not register without
// the hoist.

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
#include "../controllers/sensorcalibrationcontroller.h"
#include "../history/shothistoryexporter.h"
#include "../core/profilestorage.h"
#include "../mcp/mcpserver.h"
#include "../machine/steamhealthtracker.h"
#include "../models/flowcalibrationmodel.h"
#include "../ble/scaledeviceproxy.h"
#include "../ble/refractometers/refractometerproxy.h"
// iOS has no USB path at all: CMakeLists.txt builds none of src/usb/ there and does not
// find_package or link Qt6::SerialPort, while usbmanager.h/usbscalemanager.h include <QSerialPort>
// on every platform except Android. Including them here unguarded broke the iOS release build with
// `fatal error: 'QSerialPort' file not found` — see the note above USBManagerForeign below.
#ifndef Q_OS_IOS
#include "../usb/usbmanager.h"
#include "../usb/usbscalemanager.h"
#endif

// Guarded on DECENZA_SIMULATOR, which is what ghcsimulator.h itself guards on: the class drives
// DE1Simulator and cannot exist without it. True for every desktop configuration and for Debug on
// Android and iOS; a tablet production build has neither the simulator nor this type.
#ifdef DECENZA_SIMULATOR
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
    // MEMBER READ on the name reads undefined, and nothing says why. (The name itself stays a
    // truthy object — see the note on decenzaOptionalSingleton() below.)
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
// name main() unconditionally owns and wrong for one whose object is optional on a build that does
// register the type — a qCritical on every launch would be noise reporting the intended state.
//
// GHCSimulator is the only caller. USBManager and UsbScaleManager were the original two and are
// not any more: their registration and their publish are now behind the SAME `#ifndef Q_OS_IOS`,
// so there is no build where the type exists and the instance does not, and a null there would be
// a real publish-order defect that this quiet form would hide. They use the loud form.
//
// CRITICAL, AND NOT WHAT THIS COMMENT USED TO SAY: a null instance does NOT make the name read as
// `undefined` in QML. It used to claim that, and every `typeof X !== "undefined"` guard written
// against this class was relying on it. The Qt 6.11.1 sources say otherwise:
//
//   - qv4qmlcontext.cpp:229 resolves a singleton NAME with `QQmlTypeWrapper::create(v4, nullptr,
//     r.type)`. It calls singletonInstance<QObject*>() and DISCARDS the result — the wrapper is
//     built whether or not there is an instance.
//   - QQmlTypeWrapper has no virtualToBoolean override, so that wrapper is a truthy Object and
//     `typeof` on it is "object".
//   - Only the MEMBER READ degrades: qqmltypewrapper.cpp:319 fails its
//     `if (QObject *qobjectSingleton = ...)` and falls through to Object::virtualGet, giving
//     `undefined`.
//
// So on a build where the TYPE is registered but the INSTANCE is not, `typeof X !== "undefined"`
// and `if (X)` both PASS, and the first method call throws
// `TypeError: Property '...' of object [object Object] is not a function`.
//
// Guard the MEMBER, never the name — for THIS case, which is: the type is registered on the build
// and the instance may be missing.
//
//     if (X.doThing !== undefined) X.doThing()        // correct
//     if (typeof X !== "undefined" && X) X.doThing()  // passes, then throws
//
// The other case is the opposite and needs the opposite guard: a type NOT registered on the build
// at all (USBManager and UsbScaleManager on iOS). The name then resolves to nothing, and
// qv4qmlcontext.cpp:552-553 ends the lookup with `engine->throwReferenceError(name->toQString())`
// — so the bare `X` in `X.doThing !== undefined` throws before the member is reached. Guard the
// PLATFORM there. (`typeof X` happens to be honest in this case: Runtime::TypeofName::call clears
// the exception on purpose, qv4runtime.cpp:1746-1754, "typeof doesn't throw". Do not rely on that
// asymmetry — one idiom that is right in both cases is the platform check.)
//
// A platform check (`Qt.platform.os !== "ios"`) is also fine, because it short-circuits before the
// member read — that is why the USB pair's call sites were never affected by this.
//
// The real difference from a context property is that the name is now a declared type: qmllint
// checks the member you reach for, and a typo in it fails even though the instance may be absent
// at runtime.
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

// 14 references across 2 QML files.
//
// The sensor-calibration capture controller. Registered here rather than with
// QML_ELEMENT in its own header because main() owns it: it needs DE1Device,
// MachineState and TranslationManager, none of which the QML engine can supply.
//
// A singleton rather than a per-page instance because only one calibration
// session can be running — there is one machine — and because the page must not
// be able to construct one without those dependencies.
struct SensorCalibrationControllerForeign
{
    Q_GADGET
    QML_FOREIGN(SensorCalibrationController)
    QML_SINGLETON
    QML_NAMED_ELEMENT(SensorCalibration)

public:
    inline static SensorCalibrationController* s_singletonInstance = nullptr;
    static SensorCalibrationController* create(QQmlEngine*, QJSEngine* engine)
    {
        return decenzaPublishedSingleton(s_singletonInstance, engine, "SensorCalibration");
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

// USBManager and UsbScaleManager are declared inside `#ifndef Q_OS_IOS` in main.cpp, and these two
// structs are behind the same guard, so the type and the instance appear and disappear together.
// That is why they take decenzaPublishedSingleton() and not the quiet decenzaOptionalSingleton():
// a null instance here is no longer a platform, it is a publish-order defect, and it should say so.
//
// The pair is NOT registered on iOS. This comment used to claim the opposite — that the member was
// "typed now, on every platform including iOS" — and that is what broke the v2.0.1 iOS build: the
// types cannot be named on iOS without including their headers, the headers include <QSerialPort>,
// and iOS builds no part of src/usb/ and links no SerialPort module. Typing them there was never
// one `#include` away; it needs an iOS-buildable USB header, which is work with no payoff on a
// platform that has no USB.
//
// Consequence, and it is the pre-#1687 behaviour restored: on iOS the NAMES do not resolve, so
// evaluating one throws a ReferenceError (qv4qmlcontext.cpp:552-553) rather than reading
// `undefined`. Every call site is either short-circuited on `Qt.platform.os` before the read
// (SettingsConnectionsTab's `usbAvailable`, main.qml:1576, ScaleWeightItem.qml:61) or unreachable
// behind one — the Disconnect handler at SettingsConnectionsTab.qml:622 names USBManager bare, and
// is safe only because the ColumnLayout at :541 is invisible on iOS, so the handler never runs.
// Keep it that way, and never guard these two by the member alone.
//
// Nothing is lost on the qmllint gate, which runs on Linux where both types are registered.
//
// This defect is the six-platform rule in CI_CD.md, in the smallest possible form: the include
// compiled on five platforms, and no PR-time job compiles the sixth. Checking it before merge is
// one command — `gh workflow run ios-release.yml --ref <branch> -f upload_to_appstore=false` —
// which is how this fix was verified.

// 8 references across 1 QML file.
#ifndef Q_OS_IOS
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
        return decenzaPublishedSingleton(s_singletonInstance, engine, "USBManager");
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
        return decenzaPublishedSingleton(s_singletonInstance, engine, "UsbScaleManager");
    }
};
#endif // !Q_OS_IOS

// 3 references in main.qml, plus GHCSimulatorWindow.qml which its own engine loads.
//
// The TYPE exists wherever DECENZA_SIMULATOR does; the INSTANCE only on a debug Windows/macOS
// build, where main.cpp declares it inside `#if (Q_OS_WIN || Q_OS_MACOS) && QT_DEBUG` — the GHC
// window is a second top-level window, which is meaningless on a tablet. That split is
// deliberate. Registering only where the instance can exist would leave the type out of
// Decenza.qmltypes on Linux, and since the qmllint baseline is generated on macOS while the gate
// runs on Linux, the result is a ceiling CI cannot achieve.
//
// That split is ALSO why the QML side must guard the member and not the name. This comment used
// to say main.qml's `typeof GHCSimulator !== "undefined" && GHCSimulator` "stays correct" — it
// does not, and did not. Where the type is registered and the instance is not (Linux any config,
// Windows/macOS Release, Android/iOS Debug), the name is a truthy wrapper and only member reads
// come back undefined, so that guard passes and the call throws. See decenzaOptionalSingleton()
// above for the Qt sources. main.qml now tests `GHCSimulator.mainWindowActivated !== undefined`.
#ifdef DECENZA_SIMULATOR
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

// 79 references across 11 QML files — the largest single name in the tree, and the last context
// property to go apart from Refractometer. (The file count read 18 until #1687 review; an
// exhaustive `git grep -lw ScaleDevice -- qml/` at the pre-change commit returns 11.)
//
// Registered as ScaleDeviceProxy but NAMED ScaleDevice, so every existing call site is unchanged.
// The name has to be re-pointed as hardware comes and goes (11 sites in main.cpp) and a singleton
// cannot be, so the singleton is the proxy and the proxy is what moves. See scaledeviceproxy.h.
struct ScaleDeviceForeign
{
    Q_GADGET
    QML_FOREIGN(ScaleDeviceProxy)
    QML_SINGLETON
    QML_NAMED_ELEMENT(ScaleDevice)

public:
    inline static ScaleDeviceProxy* s_singletonInstance = nullptr;
    static ScaleDeviceProxy* create(QQmlEngine*, QJSEngine* engine)
    {
        return decenzaPublishedSingleton(s_singletonInstance, engine, "ScaleDevice");
    }
};

// 12 references, almost all in PostShotReviewPage.qml — SettingsConnectionsTab.qml also reads
// `Refractometer.supportsAutoTest` behind a typeof guard. The last context property.
//
// Registered as RefractometerProxy but NAMED Refractometer, for the same reason as ScaleDevice:
// main() re-points the name (five sites, at a DiFluid driver on connect and back at nullptr on
// disconnect) and a singleton cannot be re-pointed. Unlike the scale, "no device" here is the
// NORMAL state — a refractometer is only connected while the review page has it open — so every
// getter on the proxy is written for that case first.
struct RefractometerForeign
{
    Q_GADGET
    QML_FOREIGN(RefractometerProxy)
    QML_SINGLETON
    QML_NAMED_ELEMENT(Refractometer)

public:
    inline static RefractometerProxy* s_singletonInstance = nullptr;
    static RefractometerProxy* create(QQmlEngine*, QJSEngine* engine)
    {
        return decenzaPublishedSingleton(s_singletonInstance, engine, "Refractometer");
    }
};
