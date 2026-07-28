#pragma once

// QML registration for objects main() owns and used to publish with setContextProperty().
//
// WHY THIS FILE EXISTS SEPARATELY FROM THE CLASS HEADERS
// -----------------------------------------------------
// The obvious move is to put QML_ELEMENT straight in each class header, the way
// CoffeeBagStorage and the types reachable through MainController do it. That is fine for a
// header only the app compiles, and wrong for these: the classes here are also compiled into
// test and tool targets that link no Qt6::Qml. add_decenza_test() links Test/Core/Bluetooth/
// Sql/Network/Gui and nothing else, and `saw_parity` is narrower still — a <QtQml/...> include
// reaching either is a build break, not a style question. That is the same reason settings.h
// keeps its registration in settings_qml.h; this file is that pattern generalised.
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
// The 30 context properties still live when this file was written account for 943 of those
// warnings; the names registered BELOW are 660 of the 943. The rest are listed in
// openspec/changes/fix-qmllint-usability/tasks.md 2.4, each blocked on something specific —
// runtime swaps needing a façade, declaration-order hoists, or types already registered
// uncreatable in their own headers, where a QML_FOREIGN here would be a second registration of
// the same C++ type.
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
// ghcSimulator is a different case and is NOT here for a different reason. Its declaration is
// already inside `#if (Q_OS_WIN || Q_OS_MACOS) && QT_DEBUG`, so on every other build the instance
// legitimately does not exist — QML guards the name, and main.qml:861 has truthy-guarded it all
// along. Registering it would therefore be safe at the QML level but would make
// decenzaPublishedSingleton() qCritical on every Android, iOS and Linux launch, because that
// helper treats "asked for before main() published it" as always a defect, which for this one
// name it is not. Registering it needs an explicit optional-singleton path in the helper; that is
// a deliberate decision, not an oversight, and it is not worth taking for 15 warnings.

#include <QtQml/qqmlregistration.h>
#include "qmlsingletonpublish.h"

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

// decenzaPublishedSingleton() — the shared body of every create() below — moved to
// qmlsingletonpublish.h when appinfo.h became a second caller.

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
