// ---------------------------------------------------------------------------
// Sanitizer report capture
//
// ASan is FATAL: on a use-after-free it prints its report and SIGABRTs, so the
// app is gone before it can log anything itself. The report therefore has to be
// written by the sanitizer runtime directly, to a file that survives the crash
// — otherwise it lands on a stderr nobody was watching and the only evidence a
// user can give you is "it closed".
//
// __asan_default_options / __ubsan_default_options are weak hooks the runtimes
// call during their own init — see the constraints on them below; they are far
// tighter than they look. Each runtime appends ".<pid>" to log_path, so
// concurrent runs never collide.
//
// Whether this build is instrumented at all is decided by core/sanitizers.h,
// which explains why three separate detection routes are needed. It is included
// before everything else deliberately: these hooks must be compiled or not
// compiled, and that decision cannot depend on Qt headers.
#include "core/sanitizers.h"

#ifdef DECENZA_SANITIZERS_PRESENT

// These two hooks are called by the sanitizer runtimes during their OWN
// initialisation, which happens from __malloc_init — before libSystem is
// initialised, before the C++ runtime, before main(). Almost nothing is legal
// here, and each function may do exactly one thing: return a pointer to a
// string literal.
//
// This took two crashes to get right, both SIGSEGV before main():
//
//   attempt 1: static const std::string built by a lambda
//              -> __cxa_guard_acquire + malloc, re-entering the allocator that
//                 was still initialising.   crash in sanitizerReportDir()
//   attempt 2: static char buffer + getenv + snprintf
//              -> "allocation-free", but snprintf is still libc, and libc is
//                 not up yet.               crash in buildSanitizerLogPath()
//
// So the paths are compile-time literals. That rules out $HOME, which is why
// the reports land in /tmp rather than beside the app's other data — /tmp needs
// no lookup and no mkdir (a log_path whose directory does not exist silently
// falls back to stderr, which would defeat the point). announceSanitizerReports()
// reads them from there later, where normal code rules apply.
//
// An ASAN_OPTIONS / UBSAN_OPTIONS environment variable still overrides these.
// They are defaults, not overrides, which is what makes baking them in safe.

// Prefixes; each runtime appends ".<pid>", so concurrent runs never collide.
#define DECENZA_ASAN_LOG  "/tmp/decenza-asan"
#define DECENZA_UBSAN_LOG "/tmp/decenza-ubsan"

extern "C" const char* __asan_default_options()
{
    // halt_on_error stays at ASan's default (fatal). Continuing past a
    // use-after-free means reading memory the allocator has already handed out
    // again, so everything after the first report is fiction.
    return "log_path=" DECENZA_ASAN_LOG ":print_stacktrace=1:detect_leaks=0";
}

extern "C" const char* __ubsan_default_options()
{
    // Recovering, matching the Debug build's -fsanitize-recover: a developer
    // running the app should be TOLD about undefined behaviour, not have the app
    // killed mid-session. The report reaches the file either way.
    return "log_path=" DECENZA_UBSAN_LOG ":print_stacktrace=1:halt_on_error=0";
}

// The Qt-side reporting function is defined further down, after the Qt headers.
// Unlike these hooks it runs normally from main() and may allocate freely.

#endif  // DECENZA_SANITIZERS_PRESENT

#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScreen>
#include <QSettings>
#include <QIcon>
#include <QTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QFont>
#include <QFontDatabase>
#include <QFontInfo>
#include <QFontMetrics>
#include <QAccessible>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStandardPaths>
#include <QPixmapCache>
#include <QSysInfo>
#include <memory>
#include <vector>
#include "core/storagelogging.h"
#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QMetaEnum>
#include <QNetworkInformation>
#ifdef Q_OS_MACOS
#include <QProcess>
#endif
#include "version.h"

#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QJniEnvironment>
#endif



#include "core/asynclogger.h"
#include "core/btlogfilter.h"
#include "core/appsettings.h"
#include "core/settings.h"
#include "core/settings_qml.h"   // SettingsForeign — QML singleton registration
#include "core/contextsingletons_qml.h"  // *Foreign — singletons replacing setContextProperty()
#include "core/settingsstoremigration.h"
#include "core/settings_mqtt.h"
#include "core/settings_autowake.h"
#include "core/settings_hardware.h"
#include "core/settings_ai.h"
#include "core/settings_theme.h"
#include "core/settings_visualizer.h"
#include "core/settings_mcp.h"
#include "core/settings_brew.h"
#include "core/settings_dye.h"
#include "core/settings_network.h"
#include "core/settings_app.h"
#include "core/settings_calibration.h"
#include "core/translationmanager.h"
#include "core/batterymanager.h"
#include "core/memorymonitor.h"
#include "core/accessibilitymanager.h"
#include "core/autowakemanager.h"
#include "core/databasebackupmanager.h"
#include "core/crashhandler.h"
#include "network/crashreporter.h"
#include "core/profilestorage.h"
#include "ble/blemanager.h"
// For the [DE1][Simulator] attach line below — main.cpp owns the simulator's
// lifetime, so it is the only place that can report it.
#include "ble/de1logging.h"
#include "ble/de1device.h"
#include "ble/de1transport.h"
#ifndef Q_OS_IOS
#include "usb/usbmanager.h"
#include "usb/usbscalemanager.h"
#include "usb/usbhotplug.h"
#include "ble/scales/scalelogging.h"
#include "usb/usbdecentscale.h"
#include "usb/serialtransport.h"
#endif
#include "ble/scaledevice.h"
#include "ble/scales/scalefactory.h"
#include "ble/scales/scaletypeids.h"
#include "ble/scales/flowscale.h"
#include "ble/scales/decentscalewifi.h"
#include "ble/refractometers/difluidr1.h"
#include "ble/refractometers/difluidr2.h"
#include "ble/refractometers/refractometerdevice.h"
#if defined(Q_OS_IOS) || defined(Q_OS_MACOS)
#include "ble/transport/corebluetooth/corebluetoothscalebletransport.h" // IWYU pragma: keep
#else
#include "ble/transport/qtscalebletransport.h" // IWYU pragma: keep
#endif
#include "core/fontlogging.h"
#include "core/networklogging.h"
#include "machine/machinestate.h"
#include "machine/sawlogging.h"
#include "machine/weightprocessor.h"
#include "models/shotdatamodel.h"
#include "widget/machinestatussnapshot.h"
#include "models/steamdatamodel.h"
#include "machine/steamhealthtracker.h"
#include "controllers/maincontroller.h"
#include "controllers/profilemanager.h"
#include "controllers/shottimingcontroller.h"
#include "ai/aimanager.h"
#include "ai/aiconversation.h"
#include "screensaver/screensavervideomanager.h"
#if defined(Q_OS_IOS) || defined(Q_OS_MACOS)
#include "screensaver/iosbrightness.h"
#endif
#include "network/webdebuglogger.h"
#include "core/widgetlibrary.h"
#include "history/shothistoryexporter.h"
#include "history/shotprojection.h"
#include "mcp/mcpserver.h"
#include "mcp/mcpremoteaccess.h"
#include "network/librarysharing.h"
#include "network/relayclient.h"
#include "weather/weathermanager.h"
#include "models/flowcalibrationmodel.h"

// Simulator engine (absent from tablet release builds — see DECENZA_SIMULATOR
// in CMakeLists.txt) and GHC window (desktop debug only)
#ifdef DECENZA_SIMULATOR
#include "simulator/de1simulator.h"
#include "simulator/simulatedscale.h"
#endif
// DECENZA_SIMULATOR is part of the condition, not decoration: GHCSimulator guards its own
// contents on it (it drives DE1Simulator and cannot compile without it), so every site that
// names the class has to agree. Desktop defines it unconditionally, which is why the two used to
// look interchangeable — until `-DDECENZA_SIMULATOR_OVERRIDE=0`, the flag that exists precisely
// so the tablet-production shape can be built on a desktop, made them differ.
#if (defined(Q_OS_WIN) || defined(Q_OS_MACOS)) && defined(QT_DEBUG) && defined(DECENZA_SIMULATOR)
#include "simulator/ghcsimulator.h"
#endif

using namespace Qt::StringLiterals;

// Generated by qmltyperegistrar into decenza_qmltyperegistrations.cpp; registers the module's
// QML_ELEMENT types. Declared by hand because Qt emits no header for it. See the call site in
// main() for why it has to be invoked explicitly rather than left to Qt's lazy import path.
extern void qml_register_types_Decenza();

namespace {

// True when the saved scale address is one the BLE/WiFi reconnect ladder can
// actually dial. Two prefixes are excluded, for the same reason in both cases —
// arming the ladder for them spins a timer that can only ever no-op:
//   "usb:" — owned by UsbScaleManager, which reconnects via usbScaleAvailable.
//   "sim:" — the debug simulator's synthetic primary. It is promoted to primary
//            whenever simulation mode is on and no real scale was ever paired,
//            it appears in the Known Devices picker like any other entry, and
//            BLEManager::tryDirectConnectToScale refuses it.
// Kept as one predicate so a future third prefix is added once rather than at
// each of the ladder's arming sites.
bool scaleAddressIsLadderDialable(const QString& address)
{
    return !address.isEmpty()
        && !address.startsWith(QStringLiteral("usb:"), Qt::CaseInsensitive)
        && !address.startsWith(QStringLiteral("sim:"), Qt::CaseInsensitive);
}

// Wires a WiFi scale driver to its collaborators. Called from BOTH places that
// create or re-adopt one, because the set of things a driver needs injected is a
// property of the driver, not of the call site — and it was already drifting:
// the two sites each hand-rolled the same resolver/cache pair, so a third
// dependency meant remembering to add it twice.
void wireWifiScaleDriver(DecentScaleWifi* wifi, Settings& settings, BLEManager& bleManager)
{
    wifi->setIpResolver([&settings](const QString& host) {
        return settings.network()->wifiScaleIp(host);
    });
    wifi->setIpCacheUpdate([&settings](const QString& host, const QString& ip) {
        settings.network()->setWifiScaleIp(host, ip);
    });
    // Repeating connect failures share BLEManager's per-message warn budget, so
    // the driver's half of a dead reconnect cycle goes quiet at the same point
    // the manager's half does. Previously only the manager's was budgeted, which
    // left this driver warning once per 60 s forever with no attempt number and
    // no outcome beside it.
    wifi->setRepeatFailureSink([&bleManager](const QString& message, bool warn) {
        bleManager.scaleRepeatFailure(
            message,
            warn ? BLEManager::RepeatTier::Warn : BLEManager::RepeatTier::Info,
            QStringLiteral("BLE DecentScaleWifi"));
    });
}

constexpr const char* kAppNameOld = "Decenza DE1";
constexpr const char* kAppNameNew = "Decenza";
constexpr const char* kMigrationKey = "migration/app_name_decenza_de1_to_decenza_done";

struct MergeResult {
    int moved = 0;
    int copiedFallback = 0;
    int skipped = 0;
    int failed = 0;
};

QString appScopedPathForName(QStandardPaths::StandardLocation location, const QString& appName)
{
    const QString originalName = QCoreApplication::applicationName();
    QCoreApplication::setApplicationName(appName);
    const QString path = QDir::cleanPath(QStandardPaths::writableLocation(location));
    QCoreApplication::setApplicationName(originalName);
    return path;
}

MergeResult mergeDirectoryContents(const QString& sourceRoot, const QString& destRoot)
{
    MergeResult result;
    QDir sourceDir(sourceRoot);
    if (!sourceDir.exists()) {
        return result;
    }

    // Fast path: move whole directory when destination doesn't exist yet.
    if (!QDir(destRoot).exists()) {
        const QString destParent = QFileInfo(destRoot).absolutePath();
        if (!QDir().mkpath(destParent)) {
            qWarning() << "AppNameMigration: Failed to create destination parent directory:" << destParent;
            result.failed++;
            return result;
        }
        if (QDir().rename(sourceRoot, destRoot)) {
            result.moved++;
            return result;
        }
    }

    if (!QDir().mkpath(destRoot)) {
        qWarning() << "AppNameMigration: Failed to create destination directory:" << destRoot;
        result.failed++;
        return result;
    }

    QDirIterator it(sourceRoot, QDir::NoDotAndDotDot | QDir::AllEntries, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo sourceInfo = it.fileInfo();
        const QString relativePath = sourceDir.relativeFilePath(sourceInfo.absoluteFilePath());
        const QString destPath = QDir(destRoot).filePath(relativePath);

        if (sourceInfo.isDir()) {
            if (!QDir().mkpath(destPath)) {
                qWarning() << "AppNameMigration: Failed to create subdirectory:" << destPath;
                result.failed++;
            }
            continue;
        }

        if (QFileInfo::exists(destPath)) {
            result.skipped++;
            continue;
        }

        const QString destParent = QFileInfo(destPath).absolutePath();
        if (!QDir().mkpath(destParent)) {
            qWarning() << "AppNameMigration: Failed to create parent directory:" << destParent;
            result.failed++;
            continue;
        }

        QFile sourceFile(sourceInfo.absoluteFilePath());
        if (sourceFile.rename(destPath)) {
            result.moved++;
            continue;
        }

        // Fallback for edge cases where rename isn't possible.
        if (QFile::copy(sourceInfo.absoluteFilePath(), destPath)) {
            QFile::remove(sourceInfo.absoluteFilePath());
            result.copiedFallback++;
        } else {
            qWarning() << "AppNameMigration: Failed to copy file:" << sourceInfo.absoluteFilePath()
                       << "->" << destPath;
            result.failed++;
        }
    }

    // Best-effort cleanup of empty source directories.
    QStringList subdirs;
    QDirIterator dirIt(sourceRoot, QDir::NoDotAndDotDot | QDir::Dirs, QDirIterator::Subdirectories);
    while (dirIt.hasNext()) {
        dirIt.next();
        subdirs.prepend(dirIt.filePath());
    }
    for (const QString& subdir : subdirs) {
        QDir().rmdir(subdir);
    }
    QDir().rmdir(sourceRoot);

    return result;
}

void migrateDefaultQSettingsFromOldAppName(int& copied, int& skipped)
{
    const QString originalName = QCoreApplication::applicationName();

    QCoreApplication::setApplicationName(kAppNameOld);
    QSettings oldSettings;
    const QStringList oldKeys = oldSettings.allKeys();

    QCoreApplication::setApplicationName(kAppNameNew);
    QSettings newSettings;
    for (const QString& key : oldKeys) {
        if (newSettings.contains(key)) {
            skipped++;
            continue;
        }
        newSettings.setValue(key, oldSettings.value(key));
        copied++;
    }
    newSettings.sync();

    QCoreApplication::setApplicationName(originalName);
}

void runAppNameMigrationOnce()
{
    if (QCoreApplication::applicationName() != QLatin1String(kAppNameNew)) {
        return;
    }

    // Guard lives in the canonical store. It used to live in the legacy DE1Qt
    // store, which runSettingsStoreMigrationOnce() destroys — leaving the flag
    // there would re-run this migration on every launch forever.
    AppSettings migrationSettings;
    if (migrationSettings.value(kMigrationKey, false).toBool()) {
        return;
    }

    int settingsCopied = 0;
    int settingsSkipped = 0;
    migrateDefaultQSettingsFromOldAppName(settingsCopied, settingsSkipped);

    int filesMoved = 0;
    int filesCopiedFallback = 0;
    int filesSkipped = 0;
    int filesFailed = 0;
    const std::vector<QStandardPaths::StandardLocation> locations = {
        QStandardPaths::AppDataLocation,
        QStandardPaths::AppLocalDataLocation,
        QStandardPaths::CacheLocation
    };
    QSet<QString> migratedPairs;
    for (QStandardPaths::StandardLocation location : locations) {
        const QString oldPath = appScopedPathForName(location, kAppNameOld);
        const QString newPath = appScopedPathForName(location, kAppNameNew);
        if (oldPath.isEmpty() || newPath.isEmpty() || oldPath == newPath) {
            continue;
        }

        const QString migrationPair = oldPath + "->" + newPath;
        if (migratedPairs.contains(migrationPair)) {
            continue;
        }
        migratedPairs.insert(migrationPair);

        if (!QDir(oldPath).exists()) {
            continue;
        }

        const MergeResult merge = mergeDirectoryContents(oldPath, newPath);
        filesMoved += merge.moved;
        filesCopiedFallback += merge.copiedFallback;
        filesSkipped += merge.skipped;
        filesFailed += merge.failed;
    }

    migrationSettings.setValue(kMigrationKey, true);
    migrationSettings.sync();

    qInfo() << "AppNameMigration: completed"
            << "settingsCopied=" << settingsCopied
            << "settingsSkipped=" << settingsSkipped
            << "filesMoved=" << filesMoved
            << "filesCopiedFallback=" << filesCopiedFallback
            << "filesSkipped=" << filesSkipped
            << "filesFailed=" << filesFailed;
}

}  // namespace

#ifdef DECENZA_SANITIZERS_PRESENT
// Fold sanitizer report files into the app's own log.
//
// ASan aborts the process, so the run that produced a report cannot report it —
// the evidence exists only as a file, and until something reads that file the
// user's account of the incident is "it closed". Putting it in the debug log is
// what makes it reachable over MCP rather than only to whoever had a terminal
// open at the time.
//
// Called at startup for reports left by a previous run, and again at shutdown
// for anything UBSan produced during this one (UBSan recovers, so its findings
// would otherwise wait for the next launch). Files are renamed .reported once
// announced, so a finding is reported once and not on every launch forever.
void announceSanitizerReports(const QString& whenLabel)
{
    // Matches the literal paths in the __*_default_options hooks above; each
    // runtime appends ".<pid>" to the prefix.
    QDir dir(QStringLiteral("/tmp"));
    const QStringList found =
        dir.entryList({QStringLiteral("decenza-asan.*"),
                       QStringLiteral("decenza-ubsan.*")},
                      QDir::Files, QDir::Time);
    int announced = 0;
    int unreadable = 0;
    for (const QString& name : found) {
        if (name.endsWith(QStringLiteral(".reported")))
            continue;
        QFile f(dir.filePath(name));
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            // Counted, not skipped silently. A bare `continue` here would leave
            // `announced` at zero and print "none pending" below while an
            // unreadable report sat in the directory — the exact conflation the
            // else-branch was added to prevent, reintroduced one level up.
            ++unreadable;
            continue;
        }
        // Cap the excerpt — a report can run to hundreds of lines, and the point
        // is to make the finding visible, not to move the file into the log. The
        // full path is logged so the rest is findable.
        const QByteArray head = f.read(4000);
        f.close();
        if (head.trimmed().isEmpty()) {
            // An ASan report truncated to zero bytes by the abort. Still
            // evidence that something happened; do not count it as "none".
            ++unreadable;
            continue;
        }
        qWarning().noquote() << "SANITIZER REPORT (" << whenLabel << ") —"
                             << dir.filePath(name) << "\n"
                             << QString::fromUtf8(head).trimmed();
        // The rename is what stops a finding being re-announced forever, so
        // its failure has to be visible. It fails when the destination already
        // exists — and it will, because the runtimes append ".<pid>" and PIDs
        // are reused. Without this branch a fixed crash gets re-diagnosed on
        // every launch, in a log that assistants read over MCP and act on.
        if (!QFile::rename(dir.filePath(name),
                           dir.filePath(name + QStringLiteral(".reported")))) {
            qWarning().noquote()
                << "Sanitizer: could not mark" << dir.filePath(name)
                << "as reported — it will be announced again next launch."
                << "Delete it by hand once the finding is dealt with.";
        }
        ++announced;
    }
    if (announced > 0) {
        qWarning().noquote()
            << "Sanitizer: announced" << announced << "report(s) from" << whenLabel
            << "- a sanitizer does not write one unless it detected something, so"
            << "treat each as real until the source location says otherwise.";
    }
    if (unreadable > 0) {
        qWarning().noquote()
            << "Sanitizer:" << unreadable << "report file(s) in" << dir.path()
            << "could not be read or were empty. Something was written; its"
            << "contents are lost. Inspect them by hand.";
    }
    if (announced == 0 && unreadable == 0) {
        // Say so even when there is nothing. Otherwise silence means either
        // "scanned, found nothing" or "never ran" — and those are the two
        // readings a reporting mechanism must never conflate. The first version
        // of this function logged only on a finding, so its own correctness was
        // unobservable: exactly the ambiguity the sanitizer canary exists to
        // remove, reproduced in the code that reports sanitizer results.
        //
        // Gated on `unreadable` too: claiming "none pending" while an
        // unreadable file sits there would be the same lie in a new place.
        qDebug().noquote() << "Sanitizer report scan (" << whenLabel
                           << "): none pending in" << dir.path();
    }
}
#endif  // DECENZA_SANITIZERS_PRESENT

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

#if defined(Q_OS_MACOS) || defined(Q_OS_IOS)
    // Use native AVFoundation backend instead of FFmpeg on Apple platforms.
    // The FFmpeg/VideoToolbox backend leaks ~10 MB of Metal/IOSurface memory
    // per video transition (SIGBUS crash after ~15 screensaver videos).
    // The native darwin backend manages memory through AVPlayerLayer instead.
    qputenv("QT_MEDIA_BACKEND", "darwin");
#endif

    // Install web debug logger early to capture all output
    WebDebugLogger::install();

    // Suppress Qt's spurious "Missing CAP_NET_ADMIN" bluetooth warning
    // when our own probe says caps are effective. Must run before Qt
    // Bluetooth classes are constructed.
    BtLogFilter::install();

#ifdef Q_OS_WIN
    // Windows expresses monitor scaling as a percentage (100/125/150/175/200%),
    // which Qt converts to a fractional device pixel ratio (e.g. 150% -> 1.5).
    // Non-integer ratios are Qt's own documented cause of layout/text overflow
    // and sizing artifacts (see doc.qt.io/qt-6/highdpi.html) — and 125%/150%
    // are common Windows laptop defaults, unlike macOS/Android's integer-ratio
    // scaling. Round to the nearest whole multiple so every pixel-based
    // Theme.scaled() computation lands on integer device pixels. Must be set
    // before QApplication is constructed. (#1469)
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::RoundPreferFloor);
#endif

    QApplication app(argc, argv);

    // --- Bundled UI font (issues #1469, #1537) -----------------------------
    // Decenza ships its own UI font so text glyph metrics are deterministic
    // across platforms, OEMs, and OS versions instead of inheriting each
    // device's system font — differing system-font metrics were causing text
    // to overflow/clip on some devices but not others. Registered before the
    // QML engine loads so all QML UI inherits it. QML elements that set an
    // explicit font.family (e.g. Theme.monoFontFamily) still override it.
    //
    // The font is Roboto, RENAMED to the application-specific family
    // "Decenza Sans" (see tools/rename_bundled_font.py).
    //
    // HYPOTHESIS, NOT CONFIRMED. The reasoning: registering under a widely
    // distributed name may make family lookup ambiguous on hosts that already
    // have that font installed (Windows machines commonly get a Roboto from
    // Chrome or Adobe), and if shaping and rasterization then resolve to
    // different files you get a ligature drawn as an unrelated glyph — the
    // "Profile" -> "Proule" signature of #1537.
    //
    // DISCONFIRMING EVIDENCE, recorded deliberately: a dev Mac WITH a system
    // Roboto installed rendered correctly under the old name. A name collision
    // is therefore NOT sufficient on its own, and this rename may not be the
    // fix. It is cheap insurance that removes one variable; the per-weight
    // resolution logging below is what will actually confirm or refute it.
    //
    // See also the CurveTextRendering block further down, which attributes
    // #1537 to distance-field re-caching during resize. These two explanations
    // are NOT reconciled — do not treat either as settled.
    //
    // Registration succeeding is NOT evidence the bundled font is in use — that
    // is exactly how #1537 shipped unnoticed — so we log what actually resolved.
    {
        const QStringList fontFiles = {
            QStringLiteral(":/fonts/DecenzaSans-Regular.ttf"),
            QStringLiteral(":/fonts/DecenzaSans-Medium.ttf"),
            QStringLiteral(":/fonts/DecenzaSans-Bold.ttf"),
            QStringLiteral(":/fonts/DecenzaSans-Light.ttf"),
        };

        // Collision detector: any pre-existing host family that could be picked
        // in place of ours. Logged BEFORE registration so the host's own font
        // database is visible, not our additions to it. "Roboto" stays on the
        // watch list because it is the family the bundled font used to claim, so
        // a host copy is the specific collision we care about.
        //
        // Presence here is a CORRELATE, NOT PROOF: a dev Mac with a system Roboto
        // renders correctly. Read it together with the per-weight resolution lines
        // below, which are the actual evidence.
        {
            QStringList competing;
            for (const QString& family : QFontDatabase::families()) {
                if (family.contains(QLatin1String("Decenza"), Qt::CaseInsensitive)
                    || family.contains(QLatin1String("Roboto"), Qt::CaseInsensitive)) {
                    competing << family;
                }
            }
            if (!competing.isEmpty())
                FONT_LOG_STDERR("Bundled", QStringLiteral("Host families that could collide with the bundled "
                                           "family: %1").arg(competing.join(QStringLiteral(", "))));
        }

        // Log EVERY file's outcome. Taking families.first() from the first success and
        // discarding the rest is how this block previously reported a clean bill of health
        // while a weight had quietly registered under a foreign family — the same class of
        // mistake #1537 was: treating one observation as proof about four files.
        QString bundledFamily;
        int registeredCount = 0;
        QStringList registeredFiles;
        QStringList registeredFamilies;
        for (const QString& path : fontFiles) {
            const int id = QFontDatabase::addApplicationFont(path);
            if (id < 0) {
                FONT_WARN_STDERR("Bundled", QStringLiteral("Failed to register bundled font: %1").arg(path));
                continue;
            }
            const QStringList families = QFontDatabase::applicationFontFamilies(id);
            if (families.isEmpty()) {
                // A valid id with no families: registration "succeeded" and contributed
                // nothing. This weight is unreachable at runtime.
                FONT_WARN_STDERR("Bundled",
                    QStringLiteral("Registered but exposed NO family: %1 — this weight will "
                                   "not be reachable").arg(path));
                continue;
            }
            registeredFiles << path;
            registeredFamilies << families.join(QStringLiteral(", "));
            ++registeredCount;
            if (bundledFamily.isEmpty())
                bundledFamily = families.first();
        }
        // Still EVERY file's outcome — one line when they agree, one line each when
        // they do not. The rule this block was written for is that a weight can
        // quietly register under a foreign family, and that case is exactly the one
        // that still gets a line of its own; four identical answers did not need four.
        const bool oneFamily = !registeredFamilies.isEmpty()
                               && registeredFamilies.count(registeredFamilies.first())
                                      == registeredFamilies.size();
        if (oneFamily) {
            FONT_LOG_STDERR("Bundled", QStringLiteral("Registered %1 file(s) -> %2: %3")
                     .arg(QString::number(registeredFiles.size()),
                          registeredFamilies.first(),
                          registeredFiles.join(QStringLiteral(", "))));
        } else {
            for (qsizetype i = 0; i < registeredFiles.size(); ++i) {
                FONT_LOG_STDERR("Bundled", QStringLiteral("Registered %1 -> %2")
                         .arg(registeredFiles.at(i), registeredFamilies.at(i)));
            }
        }
        if (registeredCount != fontFiles.size()) {
            FONT_WARN_STDERR("Bundled",
                QStringLiteral("PARTIAL registration — %1 of %2 files usable; some weights "
                               "unavailable").arg(registeredCount).arg(fontFiles.size()));
        }

        if (!bundledFamily.isEmpty()) {
            app.setFont(QFont(bundledFamily));
            // Publish to Theme.qml so every font role can state the family explicitly
            // rather than relying on application-font inheritance.
            SettingsTheme::setBundledFontFamily(bundledFamily);
            FONT_INFO_STDERR("Bundled", QStringLiteral("Bundled application font set: %1").arg(bundledFamily));

            // Probe the weights Theme.qml actually requests THROUGH THIS FAMILY: the
            // default, and bold (five of the eight roles set bold: true). A single
            // default-weight probe would miss a bold-only substitution entirely.
            //
            // Deliberately NOT Light/Medium. They ship as SEPARATE ID1 families
            // ("Decenza Sans Light"/"Medium") linked by typographic family ID16 — see
            // tools/rename_bundled_font.py. Probing them under "Decenza Sans" can never
            // match on platforms that register by ID1, which would flip
            // allWeightsResolved false on healthy machines and stamp the probe advance
            // "[FALLBACK FONT]" — destroying the one number this block exists to make
            // comparable between machines. They are reported below as their own families.
            struct WeightProbe { const char* label; QFont::Weight weight; };
            static const WeightProbe kProbes[] = {
                {"Regular", QFont::Normal},
                {"Bold",    QFont::Bold},
            };
            bool allWeightsResolved = true;
            QStringList resolved;
            for (const auto& p : kProbes) {
                QFont f(bundledFamily);
                f.setWeight(p.weight);
                const QFontInfo fi(f);
                // The family comparison is the real substitution signal. exactMatch() is
                // reported for information only and deliberately NOT escalated: it returns
                // false whenever ANY requested attribute was not matched exactly (size,
                // style, weight), so warning on it would emit confident wrong diagnoses.
                // These logs are read by users' AI assistants, which act on them.
                const bool familyOk = (fi.family() == bundledFamily);
                if (familyOk) {
                    resolved << QStringLiteral("%1 exactMatch=%2")
                                    .arg(QString::fromLatin1(p.label),
                                         fi.exactMatch() ? QStringLiteral("true")
                                                         : QStringLiteral("false"));
                } else {
                    allWeightsResolved = false;
                    FONT_WARN_STDERR("Resolve",
                        QStringLiteral("%1 did NOT resolve to %2 — got %3; text metrics are "
                                       "not deterministic for this weight")
                            .arg(p.label, bundledFamily, fi.family()));
                }
            }
            if (!resolved.isEmpty()) {
                FONT_LOG_STDERR("Resolve",
                    QStringLiteral("Resolved to family=%1: %2")
                        .arg(bundledFamily, resolved.join(QStringLiteral(", "))));
            }
            FONT_LOG_STDERR("Bundled",
                QStringLiteral("Styles available for %1 = %2")
                    .arg(bundledFamily,
                         QFontDatabase::styles(bundledFamily).join(QStringLiteral(", "))));
            // Light/Medium are their own families by design; report presence without
            // letting their absence contaminate allWeightsResolved. Nothing in Theme.qml
            // requests them today, so absence is informational, not a fault.
            QStringList subFamilies;
            for (const char* suffix : {" Light", " Medium"}) {
                const QString sub = bundledFamily + QString::fromLatin1(suffix);
                subFamilies << QStringLiteral("%1 %2").arg(sub,
                    QFontDatabase::families().contains(sub) ? QStringLiteral("present")
                                                            : QStringLiteral("ABSENT"));
            }
            FONT_LOG_STDERR("Bundled",
                QStringLiteral("Sub-families: %1").arg(subFamilies.join(QStringLiteral(", "))));

            // Probe metric, deliberately at a FIXED 14px and a fixed string rather
            // than the user's effective label size: the value is only useful if it
            // is comparable between two machines' logs. This exact string mirrors
            // shothistory.helpey's English fallback (one of the grid cells that
            // overflowed in #1469), kept in sync BY HAND — it is a stable yardstick,
            // not a live measurement of what the user sees. Under a non-English
            // locale the UI draws a different string entirely.
            //
            // Tagged when resolution failed, because an untagged number invites the
            // reader to diff it against a healthy machine and read the delta as a
            // rendering difference rather than as the font substitution it actually is.
            QFont metricFont(bundledFamily);
            metricFont.setPixelSize(14);
            const qreal probe = QFontMetricsF(metricFont)
                                    .horizontalAdvance(QStringLiteral("Extraction yield (%)"));
            FONT_LOG_STDERR("Probe",
                QStringLiteral("Probe advance \"Extraction yield (%)\" @14px = %1%2")
                    .arg(probe, 0, 'f', 2)
                    .arg(allWeightsResolved
                             ? QString()
                             : QStringLiteral(" [FALLBACK FONT — not comparable]")));
        } else {
            FONT_WARN_STDERR("Bundled",
                QStringLiteral("No bundled font registered (bundled font resource missing "
                               "from build) — falling back to platform default"));
        }
    }

    // Symbol fallback face, registered separately from the four weights above: it is
    // never a candidate for the primary family, and folding it into fontFiles would
    // corrupt both the "all weights registered" count and the probe metric.
    //
    // Decenza Sans carries 927 glyphs — Latin, Greek, Cyrillic, and no symbols. Every
    // arrow and geometric shape written in QML was therefore resolved by whatever the
    // host happened to offer, which is why the same screen could measure differently on
    // two machines. Noto Sans Math supplies all seven the app uses (→ ← ↗ ↕ ▶ ◀ ⧉).
    //
    // Chained AFTER the bundled family in Theme's font roles, so it only ever fills a
    // gap. Being a real text font it stays monochrome and takes the element's colour —
    // which is what emoji cannot do, and the reason symbols are not emoji here.
    {
        const int id = QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/NotoSansMath-Regular.ttf"));
        const QStringList families = id < 0 ? QStringList() : QFontDatabase::applicationFontFamilies(id);
        if (families.isEmpty()) {
            // Not fatal: symbols revert to the platform fallback they used before this
            // font existed. Warn, because the failure is otherwise invisible — the glyphs
            // still draw, just not from the bundle, and not identically across machines.
            FONT_WARN_STDERR("Symbol",
                QStringLiteral("Symbol fallback did not register — symbols will come from the "
                               "platform fallback and vary between machines"));
        } else {
            SettingsTheme::setSymbolFontFamily(families.first());
            FONT_LOG_STDERR("Symbol", QStringLiteral("Symbol fallback registered: %1").arg(families.first()));

            // Also chain it on the APPLICATION font. Theme's roles cover everything that
            // asks for one, but an element setting only font.pixelSize inherits this font
            // instead — ValueInput's gear hint is one such site — and would otherwise still
            // resolve its symbols against the host. Re-set rather than mutate: the app font
            // was assigned above, before this face existed.
            const QString primary = SettingsTheme::bundledFontFamily();
            if (!primary.isEmpty()) {
                QFont appFont;
                appFont.setFamilies({primary, families.first()});
                app.setFont(appFont);
            }
        }
    }

#if defined(Q_OS_MACOS) || defined(Q_OS_WIN) || defined(Q_OS_LINUX)
    // Use CurveTextRendering (Qt 6.7+) on all resizable desktop platforms. It
    // renders every glyph as bezier curves on the GPU, so it needs neither the
    // distance-field glyph cache nor the native bitmap path. Two problems it avoids:
    //
    //   * Windows/Linux "fi" ligature glitch (#1537): Qt's default QtTextRendering
    //     (distance fields) re-caches glyphs while the window is resized, and at
    //     certain widths the "fi" ligature drops — "Profile" briefly reads
    //     "Proule", "Profiles" reads "Proules" — then snaps back on the next size.
    //     CurveTextRendering has no distance-field cache, so the ligature is stable.
    //
    //   * macOS Apple Color Emoji crash: QtTextRendering STILL falls back to native
    //     bitmap rendering (QSGTextMaskMaterial) when a glyph run contains color-font
    //     glyphs. If CoreText shapes ANY character to Apple Color Emoji, that bitmap
    //     path hits PNGReadPlugin::InitializePluginData → CopyEmojiImage and crashes
    //     on QSGRenderThread.
    //
    //     CurveTextRendering REDUCES but does NOT eliminate this. An earlier version of
    //     this comment claimed the crash path was gone; a crash on 2026-07-18 disproved
    //     it — the stack ran through QSGTextMaskMaterial with Curve rendering active and
    //     confirmed. Curves cannot represent colour bitmaps, so Qt still falls back to
    //     the texture-mask path for colour glyphs specifically — precisely the case this
    //     was meant to cover. The real defence is never letting a colour glyph reach
    //     CoreText: route every externally-sourced string through
    //     Theme.replaceEmojiWithImg() so emoji render as bundled SVGs. Text that skips
    //     that path (that crash was GitHub release notes in a plain TextArea) is the
    //     remaining exposure.
    //
    // Mobile (Android/iOS) keeps Qt's default: those windows are fullscreen and
    // don't resize, so the ligature glitch can't occur, and the default avoids the
    // extra GPU cost of curve rendering on constrained devices. iOS shares macOS's
    // CoreText/Apple Color Emoji stack, but the CopyEmojiImage crash has only ever
    // been observed on macOS, so iOS is intentionally left on the default too.
    QQuickWindow::setTextRenderType(QQuickWindow::CurveTextRendering);
    {
        auto actual = QQuickWindow::textRenderType();
        FONT_LOG_STDERR("TextRender",
            QStringLiteral("Requested CurveTextRendering, active type: %1 (%2)")
                .arg(actual == QQuickWindow::CurveTextRendering ? QStringLiteral("Curve")
                     : actual == QQuickWindow::QtTextRendering  ? QStringLiteral("QtText")
                                                                : QStringLiteral("Native"))
                .arg(static_cast<int>(actual)));
    }
#endif

#ifdef Q_OS_MACOS
    // Probe which characters CoreText routes to Apple Color Emoji — diagnostic
    // for the CopyEmojiImage crash. If any non-emoji chars use the emoji font,
    // it explains why Qt fell back to native rendering despite QtTextRendering.
    macos_probeEmojiFont();
#endif

    // Set application metadata
    app.setOrganizationName("DecentEspresso");
    app.setOrganizationDomain("decentespresso.com");
    app.setApplicationName("Decenza");
    app.setApplicationVersion(VERSION_STRING);

    // Both migrations must complete here — before Settings and
    // AccessibilityManager are constructed, since both read the store these
    // populate. (Deliberately no line numbers: the two this carried had drifted
    // by ~130 and ~360 lines respectively.)
    //
    // Store migration runs FIRST so that the app-name migration below finds its
    // own done-flag: that flag used to live in the legacy DE1Qt store, and this
    // migration is what carries it into the canonical one. Reversed, the
    // app-name migration would see an unstamped flag and redundantly re-run.
    runSettingsStoreMigrationOnce();
    runAppNameMigrationOnce();

    // Limit Qt's pixmap cache to 32 MB (default is 10 MB on desktop but unbounded
    // growth via QML Image elements can reach 100+ MB on devices with many emoji/icon SVGs).
    // iPad 7,4 has 3 GB RAM — keep cache reasonable to avoid OOM kills.
    QPixmapCache::setCacheLimit(32 * 1024);  // 32 MB in KB

    // Set Qt Quick Controls style (must be before QML engine creation)
    QQuickStyle::setStyle("Material");

    qDebug() << "App started - version" << VERSION_STRING << "build" << versionCode()
#ifdef QT_NO_DEBUG
             << "(release)"
#else
             << "(debug)"
#endif
             << "built" << __DATE__ << __TIME__
             << "at" << QDateTime::currentDateTime().toString(Qt::ISODate);

    // Say which sanitizers are compiled in. A sanitizer that is silently not
    // applied produces exactly the same clean run as code with no defects, so
    // "is it actually on?" must be answerable without running otool over the
    // binary. It is also the first thing worth knowing when triaging a report
    // in a log — these logs are read by users' assistants over MCP, not only
    // by whoever built the binary.
    {
        QStringList activeSanitizers;
        // CMake-defined first — see the note on DECENZA_SANITIZERS_PRESENT above
        // for why compiler macros alone cannot answer this on GCC + UBSan.
#if defined(DECENZA_ASAN_ACTIVE)
        activeSanitizers << QStringLiteral("ASan");
#endif
#if defined(DECENZA_UBSAN_ACTIVE)
        activeSanitizers << QStringLiteral("UBSan");
#endif
#if defined(__has_feature)
#  if __has_feature(address_sanitizer)
        if (!activeSanitizers.contains(QStringLiteral("ASan")))
            activeSanitizers << QStringLiteral("ASan");
#  endif
#  if __has_feature(undefined_behavior_sanitizer)
        if (!activeSanitizers.contains(QStringLiteral("UBSan")))
            activeSanitizers << QStringLiteral("UBSan");
#  endif
#  if __has_feature(thread_sanitizer)
        activeSanitizers << QStringLiteral("TSan");
#  endif
#endif
        // GCC spells these as predefined macros rather than __has_feature.
#if defined(__SANITIZE_ADDRESS__)
        if (!activeSanitizers.contains(QStringLiteral("ASan")))
            activeSanitizers << QStringLiteral("ASan");
#endif
#if defined(__SANITIZE_THREAD__)
        if (!activeSanitizers.contains(QStringLiteral("TSan")))
            activeSanitizers << QStringLiteral("TSan");
#endif
        if (activeSanitizers.isEmpty())
            qDebug() << "Sanitizers: none (uninstrumented build)";
        else
            qDebug().noquote() << "Sanitizers active:" << activeSanitizers.join(QStringLiteral(", "))
                               << "- a clean run means something in this build";
    }

    // Surface any report the sanitizer runtimes wrote during a PREVIOUS run.
    //
    // ASan aborts the process, so the run that produced a report cannot report
    // it — the evidence only exists as a file, and until something reads that
    // file the user's account of the incident is "it closed". Folding it into
    // the app's own log puts it where the debug log already goes, which is what
    // makes it reachable over MCP rather than only to whoever had a terminal
    // open at the time.
    //
    // Reports are renamed to .reported once logged, so a finding is announced
    // on the next launch and not on every launch forever.
#ifdef DECENZA_SANITIZERS_PRESENT
    announceSanitizerReports(QStringLiteral("previous run"));

    // UBSan does NOT abort (it recovers, by design, so a developer running the
    // app is told about undefined behaviour rather than having it killed
    // mid-session). That means a finding produced during THIS run would
    // otherwise sit in its file until the next launch. Scanning again on the
    // way out puts it in the same session's log, where it belongs.
    //
    // ASan needs no equivalent: it aborts, so aboutToQuit never runs and the
    // next launch is the only chance to announce it.
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, []() {
        announceSanitizerReports(QStringLiteral("this run"));
    });
#endif
    qDebug() << "Platform:" << QSysInfo::prettyProductName().simplified()
             << "arch:" << QSysInfo::currentCpuArchitecture()
             << "kernel:" << QSysInfo::kernelType() << QSysInfo::kernelVersion();
    if (QScreen *screen = QGuiApplication::primaryScreen()) {
        // Windows expresses monitor scaling as a percentage (100/125/150%...),
        // which Qt turns into a devicePixelRatio that can be fractional unless
        // setHighDpiScaleFactorRoundingPolicy() rounds it — logged here so a
        // reporter's debug log actually shows what scale/DPI their box ran at
        // instead of us guessing (font/layout overflow reports, e.g. #1469).
        qDebug() << "Display:" << screen->name()
                 << "devicePixelRatio:" << screen->devicePixelRatio()
                 << "logicalDPI:" << screen->logicalDotsPerInch()
                 << "physicalDPI:" << screen->physicalDotsPerInch()
                 << "geometry:" << screen->geometry()
                 << "availableGeometry:" << screen->availableGeometry();
    }
#ifdef Q_OS_ANDROID
    {
        jint sdkInt = QJniObject::getStaticField<jint>("android/os/Build$VERSION", "SDK_INT");
        QJniObject release = QJniObject::getStaticObjectField<jstring>("android/os/Build$VERSION", "RELEASE");
        QJniObject model = QJniObject::getStaticObjectField<jstring>("android/os/Build", "MODEL");
        QJniObject mfr = QJniObject::getStaticObjectField<jstring>("android/os/Build", "MANUFACTURER");
        qDebug() << "Android" << (release.isValid() ? release.toString() : QString())
                 << "SDK:" << sdkInt
                 << "device:" << (mfr.isValid() ? mfr.toString() : QString())
                 << (model.isValid() ? model.toString() : QString());

        // Screen-reader fingerprint. TalkBack is a Play-Store app that updates
        // independently of the OS, and its handling of synthesized text-change
        // events has regressed our typing echo (issue #1300) with no OS/settings
        // change. The debug log previously carried none of this, so capture the
        // OS-level accessibility settings + TalkBack's package version here to
        // make every a11y log self-diagnosing. NOTE: TalkBack's *internal* feature
        // toggles (keyboard echo, verbosity) live in its private prefs and are not
        // readable by other apps — only these system-level settings are.
        QJniObject activity = QNativeInterface::QAndroidApplication::context();
        if (activity.isValid()) {
            QJniObject resolver = activity.callObjectMethod(
                "getContentResolver", "()Landroid/content/ContentResolver;");
            QJniEnvironment().checkAndClearExceptions();
            auto secureSetting = [&](const char *key) -> QString {
                if (!resolver.isValid())
                    return QStringLiteral("?");
                QJniObject jkey = QJniObject::fromString(QString::fromLatin1(key));
                QJniObject val = QJniObject::callStaticObjectMethod(
                    "android/provider/Settings$Secure", "getString",
                    "(Landroid/content/ContentResolver;Ljava/lang/String;)Ljava/lang/String;",
                    resolver.object(), jkey.object());
                QJniEnvironment().checkAndClearExceptions();
                return val.isValid() ? val.toString() : QString();
            };
            const QString services = secureSetting("enabled_accessibility_services");
            qDebug() << "Accessibility settings:"
                     << "enabled=" << secureSetting("accessibility_enabled")
                     << "touchExploration=" << secureSetting("touch_exploration_enabled")
                     << "QAccessible.isActive=" << QAccessible::isActive();
            qDebug() << "Accessibility services:" << services;

            // TalkBack version: the package id is the part before '/' of the first
            // enabled service component (getPackageInfo throws NameNotFound for an
            // absent package, so clear the JNI exception afterward).
            const QString pkg = services.section(u'/', 0, 0).section(u':', 0, 0).trimmed();
            if (!pkg.isEmpty()) {
                QJniObject pm = activity.callObjectMethod(
                    "getPackageManager", "()Landroid/content/pm/PackageManager;");
                QJniEnvironment().checkAndClearExceptions();
                if (pm.isValid()) {
                    QJniObject info = pm.callObjectMethod(
                        "getPackageInfo", "(Ljava/lang/String;I)Landroid/content/pm/PackageInfo;",
                        QJniObject::fromString(pkg).object(), 0);
                    QJniEnvironment().checkAndClearExceptions();
                    if (info.isValid()) {
                        QJniObject ver = info.getObjectField<jstring>("versionName");
                        QJniEnvironment().checkAndClearExceptions();
                        qDebug() << "Screen reader package:" << pkg
                                 << "version:" << (ver.isValid() ? ver.toString() : QString());
                    }
                }
            }

            // Per-service configuration (the closest readable thing to "TalkBack
            // settings"): AccessibilityServiceInfo.toString() dumps the event types
            // it subscribes to, capabilities, feedbackType, and flags. The
            // user-facing keyboard-echo/verbosity toggles are NOT here (private to
            // TalkBack), but eventTypes/flags reveal what events it accepts.
            QJniObject am = activity.callObjectMethod(
                "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;",
                QJniObject::fromString(QStringLiteral("accessibility")).object());
            QJniEnvironment().checkAndClearExceptions();
            if (am.isValid()) {
                QJniObject list = am.callObjectMethod(
                    "getEnabledAccessibilityServiceList", "(I)Ljava/util/List;",
                    (jint)0xFFFFFFFF);  // FEEDBACK_ALL_MASK
                QJniEnvironment().checkAndClearExceptions();
                if (list.isValid()) {
                    const jint n = list.callMethod<jint>("size", "()I");
                    for (jint i = 0; i < n; ++i) {
                        QJniObject svc = list.callObjectMethod(
                            "get", "(I)Ljava/lang/Object;", i);
                        QJniEnvironment().checkAndClearExceptions();
                        if (svc.isValid())
                            qDebug() << "A11y service config:" << svc.toString();
                    }
                }
            }
        }
    }
#endif

#ifdef Q_OS_MACOS
    // Re-register the app bundle with Launch Services when the version changes
    // so macOS picks up the new icon instead of serving a stale cached one.
    {
        AppSettings s;
        QString lastRegistered = s.value("internal/lastIconRegisteredVersion").toString();
        if (lastRegistered != VERSION_STRING) {
            QString bundlePath = QCoreApplication::applicationDirPath() + "/../..";
            QProcess::startDetached(
                "/System/Library/Frameworks/CoreServices.framework"
                "/Versions/A/Frameworks/LaunchServices.framework"
                "/Versions/A/Support/lsregister",
                {"-f", QFileInfo(bundlePath).canonicalFilePath()});
            s.setValue("internal/lastIconRegisteredVersion", VERSION_STRING);
            qDebug() << "Re-registered app bundle with Launch Services for icon refresh";
        }
    }
#endif

    // Startup timing - always on, lightweight. Helps diagnose ANRs on slow devices.
    // Wall clock comes from WebDebugLogger's [LOG HH:mm:ss.zzz] prefix automatically.
    QElapsedTimer startupTimer;
    startupTimer.start();
    auto checkpoint = [&startupTimer](const char* label) {
        // Not bracketed: a leading "[token]" is subsystem-marker grammar, and this is
        // one timing label, not a subsystem anyone retrieves as a group.
        qDebug().noquote() << QStringLiteral("Startup timing: %1 - %2 ms")
                                  .arg(label).arg(startupTimer.elapsed());
    };

    // Check for crash log from previous run (don't clear yet - QML will clear after user dismisses)
    QString previousCrashLog;
    QString previousDebugLogTail;
    if (CrashHandler::hasCrashLog()) {
        previousCrashLog = CrashHandler::readCrashLog();
        previousDebugLogTail = CrashHandler::getDebugLogTail(50);
        // The trailing end marker is NOT redundant with the one inside
        // previousCrashLog, and must not be tidied away. writeCrashLog() can die
        // before it writes its own closer (it demangles from a signal handler on
        // a possibly-corrupt heap), and this line is what closes the block for
        // CrashHandler::getDebugLogTail(), which strips these blocks out of the
        // tail it submits. Both markers come from CrashHandler so a respelling
        // cannot desynchronise the writer from the stripper.
        qWarning() << "=== PREVIOUS CRASH DETECTED ===";
        qWarning().noquote() << previousCrashLog;
        qWarning() << CrashHandler::kReportEnd;
    }
    checkpoint("Crash check done");

    // Create core objects
    Settings settings;
    settings.theme()->initSystemThemeDetection();

    // Font size overrides, logged here rather than in the [Font] block above because that
    // runs before Settings exists. Only roles the user actually changed are reported —
    // when everything is stock (the overwhelmingly common case) this logs nothing at all,
    // so the line's presence is itself the signal. Without it, a layout report cannot be
    // read against the text sizes the reporter is actually running (#1469).
    {
        const QVariantMap overrides = settings.theme()->fontSizeOverrides();
        if (!overrides.isEmpty()) {
            QStringList parts;
            for (auto it = overrides.constBegin(); it != overrides.constEnd(); ++it) {
                parts << QStringLiteral("%1=%2 (default %3)")
                             .arg(it.key())
                             .arg(it.value().toInt())
                             .arg(SettingsTheme::fontSizeDefaults().value(it.key()));
            }
            FONT_LOG_STDERR("Overrides", QStringLiteral("Font size overrides: %1").arg(parts.join(QStringLiteral(", "))));
        }
    }

    checkpoint("Settings");

    // Shared QNetworkAccessManager — avoids per-class NAM overhead (connection
    // pooling, reduced thread count). Passed by pointer to most HTTP consumers.
    // Exceptions: CrashReporter, LibrarySharing, ShotServer test endpoint keep own NAM.
    QNetworkAccessManager sharedNetworkManager;

    // Monitor network reachability so the debug log captures connectivity
    // changes that race with long-running downloads (issue #1089). Best-effort:
    // load fails on platforms without a backend, in which case we just don't log.
    // The enum's NAME, not its ordinal. Reachability is a Q_ENUM, so streaming it
    // with qDebug used to print "Reachability(Online)"; routing it through a
    // helper made it a QString and a static_cast<int> turned that into
    // "Initial reachability: 4", which means nothing to the person or the AI
    // reading the log. CLAUDE.md states the rule for MCP payloads ("use
    // human-readable strings for enums") and the reason is identical here.
    const auto reachabilityName = [](QNetworkInformation::Reachability r) {
        const char* key = QMetaEnum::fromType<QNetworkInformation::Reachability>()
                              .valueToKey(static_cast<int>(r));
        return key ? QString::fromLatin1(key)
                   : QStringLiteral("Unknown(%1)").arg(static_cast<int>(r));
    };
    if (QNetworkInformation::loadDefaultBackend()) {
        if (auto* info = QNetworkInformation::instance()) {
            NETWORK_LOG_STDERR("Reachability", QStringLiteral("Initial reachability: %1")
                    .arg(reachabilityName(info->reachability())));
            QObject::connect(info, &QNetworkInformation::reachabilityChanged,
                             [reachabilityName](QNetworkInformation::Reachability r) {
                NETWORK_LOG_STDERR("Reachability", QStringLiteral("Reachability changed -> %1")
                            .arg(reachabilityName(r)));
            });
        }
    } else {
        // DEBUG, not WARN. The comment above calls this best-effort, and on any
        // platform with no backend it is a permanent, unfixable, once-per-startup
        // condition — a WARN that can never be acted on trains readers to skim the
        // tier that is supposed to mean "look here". That is this change's own
        // argument, and promoting this line contradicted it.
        NETWORK_LOG_STDERR("Reachability",
            QStringLiteral("QNetworkInformation backend unavailable on this platform — "
                           "connectivity changes will not be logged"));
    }

    TranslationManager translationManager(&sharedNetworkManager, &settings);
    checkpoint("TranslationManager");
    BLEManager bleManager;

    // Two switches, deliberately kept apart:
    //   simulationMode        — "no DE1 attached". Disables DE1 BLE only.
    //   simulatedScaleEnabled — "Simulated Scale". Blocks real scale connects,
    //                           because the simulator owns the weight stream.
    // Wiring both to setDisabled() used to mean that running the DE1 simulator
    // silently disabled real scales even with Simulated Scale switched off.
    //
    // The AND is load-bearing, not defensive. The two settings have different
    // defaults — simulatedScaleEnabled defaults to TRUE unconditionally, while
    // simulationMode defaults to true only on debug Windows/macOS — and the
    // SimulatedScale object is constructed ONLY inside `if (simulationMode())`
    // further down. Reading simulatedScaleEnabled alone therefore blocks every
    // real scale on a debug iOS/Android/Linux build, and on any desktop build
    // where the DE1 simulator is off, with no simulated scale to take over and
    // no way back: the Settings row is `visible: Settings.app.simulationMode`,
    // so the switch is hidden in exactly that state. "A simulated scale owns
    // the weight stream" is only true when a simulated scale actually exists.
    const auto applyScaleSimulated = [&bleManager, &settings]() {
        bleManager.setScaleSimulated(settings.app()->simulationMode()
                                     && settings.app()->simulatedScaleEnabled());
    };
    bleManager.setDisabled(settings.app()->simulationMode());
    applyScaleSimulated();
    QObject::connect(settings.app(), &SettingsApp::simulatedScaleEnabledChanged,
                     &bleManager, applyScaleSimulated);
    // simulationMode is an input to the gate above, so it must re-evaluate it
    // too — otherwise turning the DE1 simulator off leaves the scale blocked.
    QObject::connect(settings.app(), &SettingsApp::simulationModeChanged,
                     &bleManager, applyScaleSimulated);

    DE1Device de1Device;
    de1Device.setSettings(settings.hardware());  // Heater calibration sent to firmware
    // D9: wire the persisted (build-scoped) dual-HIGH-incapable classification
    // store BEFORE any BLE connect, so a known-weak device starts both links
    // at BALANCED on the first connect (no detection window) — and a record
    // from a different build is auto-discarded (re-detect every new build).
    bleManager.setSettings(settings.hardware());
    // Wire TranslationManager so user-visible BLE error strings get i18n
    // (scale debug-log lines stay in English regardless — they're diagnostic).
    bleManager.setTranslationManager(&translationManager);
    qDebug() << "Simulation mode:" << (settings.app()->simulationMode() ? "ON" : "off");
    de1Device.setSimulationMode(settings.app()->simulationMode());  // Restore simulation mode from settings
    std::unique_ptr<ScaleDevice> physicalScale;  // Physical BLE scale (when connected)
    FlowScale flowScale;  // Virtual scale using DE1 flow data (fallback when no BLE scale)
    ShotDataModel shotDataModel;
    SteamDataModel steamDataModel;
    SteamHealthTracker steamHealthTracker;
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
    mainController.setSteamDataModel(&steamDataModel);
    mainController.setSteamHealthTracker(&steamHealthTracker);
    mainController.setTranslationManager(&translationManager);  // for LiveSteamCoach cue i18n
    checkpoint("MainController");

    // Publishes machine phase/temp/last-shot to platform-shared storage for
    // the iOS/Android Home Screen widget. Reads existing accessors only.
    MachineStatusSnapshot machineStatusSnapshot(&de1Device, &machineState);
    // shotPersisted fires once a LIVE shot is stored: post SAW-settling
    // (finalized), espresso only (steam never saves a shot), and unconditionally
    // — unlike shotEndedShowMetadata it does not depend on the post-shot-review
    // setting. It only fires for a successful save, so there is no shotId<=0
    // case here. MainController's dev-only generateFakeShotData() persists a row
    // without emitting it, so a simulated shot deliberately leaves the tile
    // alone; the old shotSaved wiring updated it for those too.
    //
    // It carries the same yield and duration that went into the row, which is
    // the whole point: this used to read shotDataModel.finalWeight() and
    // .stopTime() instead, and stopTime is written ONLY by the stop-at-weight
    // path (WeightProcessor::stopNow → markStopAt). Any other ending — manual
    // stop, profile end, volume stop, or SAW blocked by an oscillating scale —
    // left it at its -1 sentinel, WidgetLastShot::make() rejected it, and the
    // tile kept whatever it had last accepted (each rejection logged, throttled,
    // which is how the report surfaced). In one reporter's log that was every
    // shot in the file (#1658).
    QObject::connect(&mainController, &MainController::shotPersisted,
                     &machineStatusSnapshot,
                     [&machineStatusSnapshot](qint64, double durationSec, double yieldG) {
                         // setLastShot takes (yield, duration); the signal carries
                         // (duration, yield) to match its siblings in onShotEnded().
                         // The swap is here, named, and deliberate.
                         machineStatusSnapshot.setLastShot(yieldG, durationSec);
                     });

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

    // SAW learning is keyed per (profile, scale) — and "scale" has to mean the scale that
    // actually served the shot, not the one nominated as primary in Settings. The two
    // diverge whenever the WiFi-primary Half Decent Scale is unreachable and the app falls
    // back to its BLE transport: that path deliberately skips setPrimaryScale() (see
    // isFallbackConnect in the resultFound handler), so settings.scaleType() keeps answering
    // "decent-wifi" while every weight sample arrives over BLE. Writing BLE-served shots
    // into the WiFi pool corrupts a learned model the user cannot see or reset separately.
    // Observed on-device: four consecutive BLE-served shots logged scale="decent-wifi".
    //
    // Note the SEED defaults in SettingsCalibration::sensorLag() are identical for "decent"
    // and "decent-wifi" today (both 0.38), so the first-shot prediction would not differ —
    // it is the accumulated per-key pools that this protects, and that argument survives a
    // future edit to the seed table.
    //
    // Resolved by MachineState::activeScaleType(), which keys on the scale actually wired
    // into the shot path. Deliberately NOT main()'s physicalScale: a USB scale never
    // occupies it (the USB discovery handler calls physicalScale.reset() and installs
    // usbScale directly), so keying on physicalScale would leave USB shots on the saved
    // type — correct today only because the USB path always calls setPrimaryScale(), i.e.
    // by the same luck the WiFi→BLE fallback removes.
    //
    // The Calibration tab and reset_saw_learning action=profile read the same
    // property, so the pool being trained is the pool the user sees and can clear. Keeping
    // those in sync matters as much as the key itself: a write path that diverges from the
    // read path is worse than a consistently wrong key, because nothing on screen reveals it.

    // The SAW key is resolved at shot START (to pick the prediction model and sensor
    // lag) and again ~40 s later at learning time (to pick the pool to write). Those
    // are two reads of live state with a whole shot in between, and main() swaps the
    // serving scale on device events at a dozen call sites — a USB cable knocked loose
    // mid-pour is enough. Latch it once, alongside ProfileManager::latchForShot(),
    // and learn under the key that actually made the prediction.
    QString sawScaleKeyForShot;

    // The basket segment of the SAW key, latched at the same moment and for the same
    // reason: swapping the active equipment package between the stop and settling
    // completion would otherwise file the entry under a basket that did not pull the shot.
    QString sawBasketKeyForShot;

    // Connect SAW learning signal to settings persistence.
    // Logs the predicted-vs-actual drip ("accuracy" line) before persisting, so any single
    // shot's debug log records whether SAW hit its target. addSawLearningPoint then routes
    // the entry through the per-(profile, scale) batch accumulator and emits the
    // "accumulated"/"committed"/"batch rejected" qDebug line that ShotDebugLogger captures.
    QObject::connect(&timingController, &ShotTimingController::sawLearningComplete,
                     [&settings, &mainController, &machineState, &sawScaleKeyForShot,
                      &sawBasketKeyForShot](
                             double drip, double flowAtStop, double overshoot) {
                         // Prefer the latched key. Empty only if learning somehow fires
                         // without a cycle start, in which case live state is all there is.
                         const QString liveScaleType = machineState.activeScaleType();
                         const QString scaleType = sawScaleKeyForShot.isEmpty()
                                                       ? liveScaleType : sawScaleKeyForShot;
                         if (!sawScaleKeyForShot.isEmpty() && liveScaleType != sawScaleKeyForShot) {
                             // Not fatal — the latched key is the correct one to learn
                             // under — but it means the serving scale changed mid-shot,
                             // which is worth seeing in the shot log rather than inferring
                             // later from a pool that drifted.
                             SAW_WARN_STDERR("Learning",
                                 QStringLiteral("Scale changed mid-shot: predicted with %1 but now "
                                                "serving %2 — learning under the former")
                                     .arg(sawScaleKeyForShot, liveScaleType));
                         }
                         const QString profileFilename = mainController.profileManager()->baseProfileName();
                         // Same latch-or-live rule as the scale key above.
                         const QString basketKey = sawBasketKeyForShot.isEmpty()
                                                       ? settings.calibration()->currentBasketKey()
                                                       : sawBasketKeyForShot;
                         const double predictedDrip = settings.calibration()->getExpectedDripFor(
                             profileFilename, scaleType, flowAtStop, basketKey);
                         SAW_LOG_STDERR("Learning",
                             QStringLiteral("Accuracy: predictedDrip=%1 g actualDrip=%2 g "
                                            "delta=%3 g overshoot=%4 g flow=%5 g/s "
                                            "scale=%6 profile=%7 basket=%8")
                                 .arg(predictedDrip, 0, 'f', 2).arg(drip, 0, 'f', 2)
                                 .arg(drip - predictedDrip, 0, 'f', 2).arg(overshoot, 0, 'f', 2)
                                 .arg(flowAtStop, 0, 'f', 2).arg(scaleType, profileFilename, basketKey));
                         settings.calibration()->addSawLearningPoint(drip, flowAtStop, scaleType, overshoot,
                                                                    profileFilename, basketKey);
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
    weightThread.start();

    // Scale → WeightProcessor (main → worker, auto QueuedConnection)
    // Initially connected to FlowScale; reconnected when physical scale is found
    QObject::connect(&flowScale, &ScaleDevice::weightSampleReceived,
                     &weightProcessor, &WeightProcessor::processWeight);

    // WeightProcessor → DE1Device: stop-at-weight.
    // Use DirectConnection so the lambda runs immediately on the WeightProcessor's HighPriority
    // thread, then post a Qt::HighEventPriority event to DE1Device. This makes the SAW stop
    // jump ahead of any normal-priority events already queued on the main thread (e.g. D-Flow
    // setpoint writes), preventing the 4+ second delivery delay seen on slow devices.
    QObject::connect(&weightProcessor, &WeightProcessor::stopNow,
                     &weightProcessor, [&de1Device](qint64 sawTriggerMs) {
                         QCoreApplication::postEvent(&de1Device,
                             new SawStopEvent(sawTriggerMs),
                             Qt::HighEventPriority);
                     }, Qt::DirectConnection);

    // WeightProcessor → MachineState: forward SAW trigger for QML "Target reached" display
    QObject::connect(&weightProcessor, &WeightProcessor::stopNow,
                     &machineState, [&machineState](qint64) {
                         emit machineState.targetWeightReached();
                     });

    // WeightProcessor → MachineState: notify QML when SAW is bypassed (untared cup).
    // Using &machineState as context ensures lambda runs on the main thread.
    QObject::connect(&weightProcessor, &WeightProcessor::untaredCupDetected,
                     &machineState, [&machineState]() {
                         emit machineState.sawBypassed();
                     });

    // WeightProcessor → ShotDataModel: re-anchor the graph on the zero that actually
    // arrived. MachineState::tareCompleted already clears the pre-tare samples, but it
    // fires when the tare COMMAND goes out; the scale's zeroed sample lands tens of ms
    // later, and any pre-tare reading in between is appended after that clear. It then
    // becomes the spike filter's anchor there — a logged shot rejected the real 0.1 g
    // against a stale 141.1 g at 534 g/s, three times. Same event as WeightProcessor's
    // own tare wait, so the two cannot disagree about when the zero landed.
    // &shotDataModel as context puts it on the main thread.
    QObject::connect(&weightProcessor, &WeightProcessor::tareLanded,
                     &shotDataModel, [&shotDataModel]() { shotDataModel.clearWeightData(); });

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

    // WeightProcessor → MachineState: the shot's post-tare zero offset, so the live
    // readout, MQTT and MCP report the same weight SAW stops on and the shot record
    // saves. &machineState as context puts it on the main thread.
    QObject::connect(&weightProcessor, &WeightProcessor::preShotZeroOffsetChanged,
                     &machineState, &MachineState::updatePreShotZeroOffset);

    // Forward frame number updates from shot samples to worker thread.
    // With &weightProcessor as context, Qt auto-uses QueuedConnection (cross-thread).
    QObject::connect(&timingController, &ShotTimingController::sampleReady,
                     &weightProcessor, [&weightProcessor](double, double pressure, double flow, double,
                         double, double, double, int frameNumber, bool) {
                         // pressure/flow feed the step-exit arbiter (mixed-frame race guard).
                         weightProcessor.setCurrentFrame(frameNumber, pressure, flow);
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
                     [&weightProcessor, &machineState, &settings, &mainController, &timingController,
                      &sawScaleKeyForShot, &sawBasketKeyForShot]() {
                         // Freeze the resolved target + dose for the duration
                         // of the shot (add-yield-ratio-anchor Decision 9),
                         // alongside the SAW model snapshot below so the two
                         // stay consistent. While latched,
                         // ProfileManager::targetWeight() answers with the
                         // frozen value, so NO late write — a dose capture,
                         // a bean switch's override clear, a recipe
                         // activation, an MCP/web anchor write, a profile
                         // load — can re-resolve and reach the worker through
                         // the ungated forwarder below and move the live SAW
                         // target. Event-driven, released at shot end.
                         mainController.profileManager()->latchForShot();

                         // Build snapshot of learning data and configuration.
                         // Per-(profile, scale, basket) lookup falls back to the global
                         // bootstrap / pool / scale default automatically when the triple has
                         // not yet graduated (fewer than kSawMinMediansForGraduation committed
                         // batches — 1 today, so one 3-shot batch). The "Model:" log line
                         // records which source is driving this shot's predictions.
                         //
                         // The entries fetched below are the committed (drip, flow) pairs, and
                         // they go straight into the WeightProcessor snapshot that decides when
                         // to fire the stop — so a pair that describes no real shot mis-stops
                         // one. See addSawPerPairEntry's commit step.
                         double targetWeight = machineState.targetWeight();
                         QString scaleType = machineState.activeScaleType();
                         sawScaleKeyForShot = scaleType;  // latched for the learning path
                         const QString basketKey = settings.calibration()->currentBasketKey();
                         sawBasketKeyForShot = basketKey;  // latched for the same reason
                         QString profileFilename = mainController.profileManager()->baseProfileName();
                         bool converged = settings.calibration()->isSawConverged(scaleType);
                         int maxEntries = converged ? 12 : 8;
                         const auto entries = settings.calibration()->sawLearningEntriesFor(profileFilename, scaleType, maxEntries, basketKey);
                         const QString modelSource = settings.calibration()->sawModelSource(profileFilename, scaleType, basketKey);
                         const double currentLag = settings.calibration()->sawLearnedLagFor(profileFilename, scaleType, basketKey);
                         SAW_LOG_STDERR("Learning",
                             QStringLiteral("Model: source=%1 lag=%2 s profile=%3 scale=%4 basket=%5 historyN=%6")
                                 .arg(modelSource).arg(currentLag, 0, 'f', 3)
                                 .arg(profileFilename, scaleType, basketKey).arg(entries.size()));
                         QVector<double> drips, flows;
                         drips.reserve(entries.size());
                         flows.reserve(entries.size());
                         for (const auto& e : entries) {
                             drips.append(e.first);
                             flows.append(e.second);
                         }

                         // Build frame exit weights and preinfuse count from current profile
                         QVector<double> frameExitWeights;
                         // Per-frame firmware exit conditions (parallel to frameExitWeights):
                         // lets the step-exit arbiter avoid double frame-advances on frames
                         // that carry both a weight exit and a firmware pressure/flow exit.
                         QVector<FrameExitCondition> frameExitConditions;
                         const Profile& profile = mainController.profileManager()->currentProfile();
                         int preinfuseFrameCount = profile.preinfuseFrameCount();
                         {
                             const auto& steps = profile.steps();
                             frameExitWeights.reserve(steps.size());
                             frameExitConditions.reserve(steps.size());
                             for (const auto& step : steps) {
                                 frameExitWeights.append(step.exitWeight);
                                 frameExitConditions.append(FrameExitCondition::fromExitFields(
                                     step.exitIf, step.exitType,
                                     step.exitPressureOver, step.exitPressureUnder,
                                     step.exitFlowOver, step.exitFlowUnder));
                             }
                         }

                         // Tare already happened synchronously in onEspressoCycleStarted
                         bool tareComplete = timingController.isTareComplete();
                         double sensorLagSeconds = SettingsCalibration::sensorLag(scaleType);

                         QMetaObject::invokeMethod(&weightProcessor,
                             [&weightProcessor, targetWeight, preinfuseFrameCount, frameExitWeights, frameExitConditions, drips, flows, converged, tareComplete, sensorLagSeconds]() {
                                 weightProcessor.configure(targetWeight, preinfuseFrameCount, frameExitWeights, frameExitConditions, drips, flows, converged,
                                                           sensorLagSeconds);
                                 weightProcessor.startExtraction();
                                 if (tareComplete) {
                                     weightProcessor.setTareComplete(true);
                                 }
                             }, Qt::QueuedConnection);
                     });

    // Auto-tare during "flow before" phase → WeightProcessor: clear stale cup-weight data.
    // NOTE: resetForRetare() must NOT call setTareComplete() — see ordering comment above
    // (lines 548-554). A separate queued setTareComplete would race with startExtraction().
    QObject::connect(&machineState, &MachineState::flowBeforeAutoTare,
                     [&weightProcessor]() {
                         QMetaObject::invokeMethod(&weightProcessor, [&weightProcessor]() {
                             weightProcessor.resetForRetare();
                         }, Qt::QueuedConnection);
                     });

    // Mark extraction start when flow actually begins, not at preheat.
    // This ensures the untared-cup sanity check in WeightProcessor doesn't fire during
    // preheat while the BLE tare command is still in transit to the scale.
    QObject::connect(&machineState, &MachineState::shotStarted,
                     [&weightProcessor]() {
                         QMetaObject::invokeMethod(&weightProcessor, [&weightProcessor]() {
                             weightProcessor.markExtractionStart();
                         }, Qt::QueuedConnection);
                     });

    QObject::connect(&machineState, &MachineState::shotEnded,
                     [&weightProcessor]() {
                         QMetaObject::invokeMethod(&weightProcessor, [&weightProcessor]() {
                             weightProcessor.stopExtraction();
                         }, Qt::QueuedConnection);
                     });

    // Retire the shot's zero correction once the shot has been saved. shotProcessingReady
    // is the right edge: shotEnded fires while drip is still settling, and the settle
    // samples run through the same correction, so clearing there would step the graph and
    // the saved finalWeightG by the offset. Queued into the worker, and posted after the
    // direct-connected onShotEnded above has already read the shot, so the record is safe.
    // Without this the offset outlived its shot and every surface reading MachineState::
    // scaleWeight (idle readout, steam, dose weighing, MQTT, MCP, widget) stayed skewed by
    // it — and a manual tare could not fix it, because the scale zeroed while the app went
    // on subtracting. Only an app restart cleared it.
    QObject::connect(&timingController, &ShotTimingController::shotProcessingReady,
                     [&weightProcessor]() {
                         QMetaObject::invokeMethod(&weightProcessor, [&weightProcessor]() {
                             weightProcessor.clearPreShotZeroOffset();
                         }, Qt::QueuedConnection);
                     });

    // Release the shot latch on espressoCycleEnded, NOT shotEnded: the latch is
    // armed at espressoCycleStarted, and only espressoCycleEnded is that
    // signal's pair. shotEnded is gated on flow having started, so a cycle
    // aborted during preheat (stop tapped, machine aborts, BLE drops) would
    // arm the latch and never release it — freezing targetWeight() at that
    // shot's value for the rest of the session while every surface kept
    // showing the live one. The latch outliving the cycle is precisely the
    // failure this latch exists to prevent, so it is released on the broadest
    // exit rather than the narrowest. Idempotent.
    //
    // Ordering — twice written wrong here, so it is sourced. Release runs BEFORE
    // the save path, on every shot, and what guarantees it is the delivery mode,
    // not settling: espressoCycleEnded is emitted SYNCHRONOUSLY on the phase
    // transition (machinestate.cpp:724), while shotEnded — which reaches
    // onShotEnded via ShotTimingController::endShot() -> shotProcessingReady —
    // is emitted inside a Qt::QueuedConnection invokeMethod
    // (machinestate.cpp:772-784). So the release is always at least one event
    // loop turn ahead.
    //
    // Stop-at-weight settling widens that gap to ~1.4 s when it runs, but does
    // NOT establish it: on a shot with no SAW trigger, endShot() takes the else
    // branch at shottimingcontroller.cpp:150-155 and emits shotProcessingReady
    // immediately, with no settling window at all. An earlier version of this
    // comment claimed settling was the mechanism, which is false for every
    // manual, volume-stopped, GHC-button and profile-end shot.
    //
    // Harmless for the yield snapshot, which deliberately survives release. It
    // is why ProfileManager's flow-calibration latch, which does NOT survive, is
    // taken in onShotEnded() rather than cleared here.
    QObject::connect(&machineState, &MachineState::espressoCycleEnded,
                     [&weightProcessor, &mainController, &machineState]() {
                         mainController.profileManager()->releaseShotLatch();
                         // Disarm the SAW worker for the same reason and by the
                         // same asymmetry: startExtraction arms it at cycle
                         // start (above), stopExtraction hangs off shotEnded and
                         // so never runs for a cycle that never flowed, leaving
                         // SAW live against the dead shot's target until the
                         // next shot re-armed it. No-op on a normal shot.
                         QMetaObject::invokeMethod(&weightProcessor, [&weightProcessor]() {
                             weightProcessor.endShotCycle();
                         }, Qt::QueuedConnection);
                         // Same asymmetry, third time: the pre-shot zero is retired on
                         // shotProcessingReady (above), which is reached only through
                         // shotEnded. A mid-pour disconnect sets Phase::Disconnected,
                         // emits THIS signal and returns before the queued shotEnded ever
                         // fires (MachineState::updateFromDevice), so the offset a flowing
                         // shot had already adopted would stay applied to the idle scale
                         // for the rest of the session — the bug this fix exists to remove,
                         // reached by a different road.
                         //
                         // Guarded on Disconnected rather than clearing unconditionally,
                         // because this signal is emitted SYNCHRONOUSLY on the normal
                         // Ending -> Idle exit while shotEnded is queued: an unguarded
                         // clear here would land before the settle window and step the
                         // drip samples, and so the saved finalWeightG, by the offset.
                         // A disconnect has no such window — the shot is dead and nothing
                         // further will be captured from it.
                         if (machineState.phase() == MachineState::Phase::Disconnected) {
                             QMetaObject::invokeMethod(&weightProcessor, [&weightProcessor]() {
                                 weightProcessor.clearPreShotZeroOffset();
                             }, Qt::QueuedConnection);
                         }
                     });

    // Machine phase → WeightProcessor: extend scale-feed-liveness detection to
    // the pre-shot EspressoPreheating phase (BLE connection-priority backstop,
    // #1093/#1176). The feed dies during preheat prep on weak radios, ~6 s
    // before extraction — detecting it there lets the backoff + reconnect begin
    // during warm-up. True only while preheating; any other phase
    // (Idle/Sleep/Preinfusion/.../extraction-end) clears it, so a legitimately
    // idle scale never trips (m_active covers true extraction separately).
    // Cross-thread to the worker → explicit Qt::QueuedConnection, consistent
    // with the WeightProcessor wiring above.
    QObject::connect(&machineState, &MachineState::phaseChanged,
                     [&weightProcessor, &machineState]() {
                         const bool preheating =
                             machineState.phase() == MachineState::Phase::EspressoPreheating;
                         QMetaObject::invokeMethod(&weightProcessor, [&weightProcessor, preheating]() {
                             weightProcessor.setShotCycleActive(preheating);
                         }, Qt::QueuedConnection);
                     });

    // Forward live SAW target changes (e.g. user pressed +10g mid-shot) to the worker.
    // Pre-shot callers (profile activation, recipe save) also fire this signal, but
    // configure() overwrites m_targetWeight at shot start, so any pre-shot forwarding
    // is harmless. Only mid-shot bumps observably move the worker's target.
    // Ladder writes can no longer reach this path mid-shot
    // (add-yield-ratio-anchor): targetWeight() answers with the RESOLVED target
    // latched at espressoCycleStarted (see above), so neither a dose write nor
    // an anchor write/clear moves it — and therefore this forwarder — during a
    // shot. Latching only the dose would not have covered the anchor writes.
    // The deliberately mid-shot caller (the phase-gated +10 g bump) writes
    // MachineState directly rather than through the ladder, so it still flows.
    QObject::connect(&machineState, &MachineState::targetWeightChanged,
                     [&weightProcessor, &machineState]() {
                         const double w = machineState.targetWeight();
                         QMetaObject::invokeMethod(&weightProcessor, [&weightProcessor, w]() {
                             weightProcessor.setTargetWeight(w);
                         }, Qt::QueuedConnection);
                     });

#ifdef Q_OS_ANDROID
    // GC management: defer Android GC during flowing operations (espresso, hot water, etc.)
    // to reduce stop-the-world pause impact on BLE delivery and SAW latency.
    //
    // Strategy:
    //   - App startup:
    //       • Call idleGc() immediately and start the 15-minute periodic timer.
    //   - EspressoPreheating / HotWater / Flush start:
    //       • Stop the periodic timer (no GC right before or during a shot).
    //       • Raise heap utilization threshold to 0.95 (GC only if heap is 95% full).
    //         No explicit System.gc() here — GC near preinfusion is worse than no GC.
    //   - Returning to Idle/Ready:
    //       • onFlowingEnded() resets the heap threshold and runs an immediate GC.
    //       • Restart the 15-minute periodic timer for ongoing idle maintenance.
    //
    // During extended idle (screensaver, overnight) the timer fires every 15 minutes
    // to prevent unbounded Java heap growth from BLE GATT callbacks.
    //
    // s_inOperation prevents double-calls as the machine moves through sub-phases
    // (EspressoPreheating → Preinfusion → Pouring → Ending).

    auto* idleGcTimer = new QTimer();
    idleGcTimer->setSingleShot(false);
    idleGcTimer->setInterval(15 * 60 * 1000);  // 15-minute periodic idle GC
    QObject::connect(idleGcTimer, &QTimer::timeout, []() {
        QJniObject::callStaticMethod<void>(
            "io/github/kulitorum/decenza_de1/BleHelper",
            "idleGc", "()V");
    });

    QObject::connect(&machineState, &MachineState::phaseChanged,
                     [&machineState, idleGcTimer]() {
        using Phase = MachineState::Phase;
        static bool s_inOperation = false;
        const Phase phase = machineState.phase();

        const bool enteringOp = !s_inOperation && (
            phase == Phase::EspressoPreheating ||   // earliest signal for espresso
            phase == Phase::HotWater ||
            phase == Phase::Steaming ||
            phase == Phase::Flushing ||
            phase == Phase::Descaling ||
            phase == Phase::Cleaning ||
            phase == Phase::Transport);

        const bool exitingOp = s_inOperation && (
            phase == Phase::Idle ||
            phase == Phase::Ready ||
            phase == Phase::Sleep ||
            phase == Phase::Disconnected);

        if (enteringOp) {
            s_inOperation = true;
            idleGcTimer->stop();  // Pause periodic GC during operations
            QJniObject::callStaticMethod<void>(
                "io/github/kulitorum/decenza_de1/BleHelper",
                "onFlowingStarted", "()V");
        } else if (exitingOp) {
            s_inOperation = false;
            QJniObject::callStaticMethod<void>(
                "io/github/kulitorum/decenza_de1/BleHelper",
                "onFlowingEnded", "()V");  // runs immediate post-shot GC
            idleGcTimer->start();  // Resume periodic idle GC
        }
    });

    // Run GC at startup and start the periodic idle timer. The app starts idle
    // and onFlowingEnded() won't fire until the first shot ends, so without this
    // the heap accumulates BLE stack garbage unchecked until the first shot.
    QJniObject::callStaticMethod<void>(
        "io/github/kulitorum/decenza_de1/BleHelper",
        "idleGc", "()V");
    idleGcTimer->start();

    // BLE dead-binder recovery: when the Bluetooth GATT binder/process dies
    // (toggled off, OEM power policy, GATT proxy unbound) Qt's BLE handler
    // thread raises a DeadObjectException (or its DeadSystemException
    // subclass if system_server itself died). The Java crash handler catches
    // both, keeps the app alive, and writes a flag file; we poll for it
    // every 10 s and trigger BLE reconnection if found. Issues #189, #1227.
    auto* bleRecoveryTimer = new QTimer();
    bleRecoveryTimer->setInterval(10000);
    QObject::connect(bleRecoveryTimer, &QTimer::timeout,
                     [&bleManager, &de1Device]() {
        // Use same path as CrashHandler (proven to match Java getFilesDir())
        QString flagPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                         + "/ble_dead_system";
        QFile flagFile(flagPath);
        if (!flagFile.exists())
            return;

        qWarning() << "BLE recovery: dead BLE binder detected, triggering reconnect";
        flagFile.remove();

        // The BLE handler thread is dead — Qt's QLowEnergyController won't emit
        // disconnected() on its own. Force-disconnect and re-scan.
        if (de1Device.isConnected()) {
            qWarning() << "BLE recovery: forcing DE1 disconnect";
            de1Device.disconnect();
        }
        bleManager.resetScaleConnectionState();

        // Attempt reconnection after a short delay to let Android restart Bluetooth.
        // Scale uses scan-only (allowDirectConnect=false): right after a BLE-stack
        // death a parked direct-connect to a possibly-absent scale is exactly the
        // contention we avoid (#1303); the scan reconnects it when it advertises.
        QTimer::singleShot(3000, [&bleManager]() {
            qDebug() << "BLE recovery: attempting reconnect";
            bleManager.tryDirectConnectToDE1();
            bleManager.tryDirectConnectToScale(/*allowDirectConnect=*/false);
        });
    });
    bleRecoveryTimer->start();
#endif

    checkpoint("WeightProcessor wiring");

    // Create and wire AI Manager
    AIManager aiManager(&sharedNetworkManager, &settings);
    mainController.setAiManager(&aiManager);
    // Localize AI error strings. Wired here (not via MainController's fan-out)
    // because MainController::setTranslationManager already ran above, before
    // the AIManager was attached. Forwards to every provider + the conversation.
    aiManager.setTranslationManager(&translationManager);

    // Register the per-provider model-hint strings and the per-MODEL cost
    // strings with the translation registry.
    //
    // Model hints: SettingsAITab.qml builds these keys dynamically
    // ("settings.ai.modelHint." + provider), which the QML string scanner
    // cannot see; registering here keeps the batch-translation registry
    // complete while the English copy stays in AIProvider::modelHint().
    //
    // Cost lines: the keys are static (ai.cost.<provider>.<model>) and live in
    // C++, but translateString() only registers a key when it is CALLED, and
    // the app only ever calls costHintFor() for the model currently selected.
    // Without this loop a user translates the app, switches models, and gets
    // an English cost line for a model whose string was never offered to the
    // translator. Priced models are enumerated rather than assumed, so a model
    // with no costHintFor() case simply contributes no key.
    for (const QString& providerId : aiManager.availableProviders()) {
        const QString hint = aiManager.modelHint(providerId);
        if (!hint.isEmpty())
            translationManager.translateString("settings.ai.modelHint." + providerId, hint);
        for (const QVariant& model : aiManager.availableModels(providerId)) {
            const QString modelId = model.toMap().value("id").toString();
            if (!modelId.isEmpty())
                (void)aiManager.costHint(providerId, modelId);
        }
    }

    // Connect FlowScale to graph initially (will be disconnected if physical scale found)
    QObject::connect(&flowScale, &ScaleDevice::weightChanged,
                     &mainController, &MainController::onScaleWeightChanged);

    ScreensaverVideoManager screensaverManager(&sharedNetworkManager, &settings, &profileStorage);
#ifdef Q_OS_IOS
    // Restore screen brightness if the app crashed while dimmed
    ios_checkAndRestoreBrightness();
#endif
    checkpoint("ScreensaverVideoManager");

    // Connect screensaver manager and AI manager to shot server
    mainController.shotServer()->setScreensaverVideoManager(&screensaverManager);
    mainController.shotServer()->setAIManager(&aiManager);
    mainController.shotServer()->setMqttClient(mainController.mqttClient());
    // Connect screensaver manager to data migration client for media import
    mainController.dataMigration()->setScreensaverVideoManager(&screensaverManager);

    // Shot-history-to-file exporter: mirrors the shots DB into individual
    // visualizer-format JSON files under ProfileStorage::userHistoryPath()
    // whenever Settings::exportShotsToFile is on.
    ShotHistoryExporter shotHistoryExporter(&settings, &profileStorage, mainController.shotHistory());

    BatteryManager batteryManager;
    batteryManager.setDE1Device(&de1Device);
    batteryManager.setSettings(&settings);

    mainController.shotServer()->setBatteryManager(&batteryManager);

    MemoryMonitor memoryMonitor;
    mainController.shotServer()->setMemoryMonitor(&memoryMonitor);

    // Widget library for saving/sharing layout items, zones, and layouts
    WidgetLibrary widgetLibrary(&settings);

    // Library sharing - upload/download widgets to/from decenza.coffee
    LibrarySharing librarySharing(&settings, &widgetLibrary);

    // Connect widget library and sharing to shot server for web layout editor
    mainController.shotServer()->setWidgetLibrary(&widgetLibrary);
    mainController.shotServer()->setLibrarySharing(&librarySharing);

    // MCP Server for AI remote control
    McpServer mcpServer;
    mcpServer.setDE1Device(&de1Device);
    mcpServer.setMachineState(&machineState);
    mcpServer.setMainController(&mainController);
    mcpServer.setProfileManager(mainController.profileManager());
    mcpServer.setShotHistoryStorage(mainController.shotHistory());
    mcpServer.setBLEManager(&bleManager);
    mcpServer.setSettings(&settings);
    mcpServer.setMemoryMonitor(&memoryMonitor);
    mcpServer.setScreensaverVideoManager(&screensaverManager);
    mcpServer.setTranslationManager(&translationManager);
    mcpServer.setBatteryManager(&batteryManager);
    mainController.shotServer()->setMcpServer(&mcpServer);

    // Remote MCP connector: a dedicated tokenized listener (separate from
    // ShotServer) that makes the MCP server reachable from Claude/ChatGPT
    // mobile custom connectors. Defaults off; started on demand from Settings.
    McpRemoteAccess remoteMcpAccess;
    remoteMcpAccess.setMcpServer(&mcpServer);
    remoteMcpAccess.setSettings(settings.mcp());
    // Expose live remote-access status + connector/login URLs to the web
    // settings page (Settings.mcp holds the persisted config).
    mainController.shotServer()->setRemoteMcpAccess(&remoteMcpAccess);
    // Note: registerAllTools() is deferred until after AccessibilityManager is created (below)

    // Relay client for Pocket app remote control via AWS WebSocket
    RelayClient relayClient(&de1Device, &machineState, &settings);
    mainController.shotServer()->setRelayClient(&relayClient);
    if (!settings.app()->pocketPairingToken().isEmpty() && settings.app()->screenCaptureEnabled()) {
        relayClient.setEnabled(true);
    }

    // React to setting changes at runtime
    QObject::connect(settings.app(), &SettingsApp::screenCaptureEnabledChanged, [&relayClient, &settings]() {
        if (settings.app()->screenCaptureEnabled() && !settings.app()->pocketPairingToken().isEmpty()) {
            relayClient.setEnabled(true);
        } else {
            relayClient.setEnabled(false);
        }
    });

#ifdef Q_OS_ANDROID
    // Quiet anything that owns a long-lived QSocketNotifier before Android's
    // PackageInstaller takes over. The system reaps our fds during the install
    // handover and Qt's UNIX event dispatcher SIGSEGVs in
    // QSocketNotifier::setEnabled if it tries to service one afterward (#865).
    // Both UpdateChecker (UI-triggered) and ShotServer (web-triggered) emit
    // aboutToDispatchInstall on the main thread immediately before the JNI
    // dispatch; the connection below uses Qt::AutoConnection which resolves
    // to Qt::DirectConnection because both signal and receiver are on the
    // main thread — the slot runs synchronously, finishes the teardown, and
    // returns before the JNI call dispatches. If either emitter ever moves
    // to a worker thread the connection silently flips to QueuedConnection
    // and the fix breaks; keep both emit sites on the main thread.
    // We don't try to restore on cancel — the install either succeeds
    // (process replaced) or fails (rare; user can restart).
    // CrashReporter is wired separately below because it's declared later.
    auto quietNetworkForApkInstall = [&mainController, &sharedNetworkManager, &relayClient, &librarySharing]() {
        qDebug() << "Quieting network services for APK install handover";
        if (auto* server = mainController.shotServer()) {
            server->stop();
        }
        sharedNetworkManager.clearConnectionCache();
        relayClient.shutdown();
        librarySharing.clearConnectionCache();
    };
    QObject::connect(mainController.updateChecker(), &UpdateChecker::aboutToDispatchInstall,
                     &mainController, quietNetworkForApkInstall);
    QObject::connect(mainController.shotServer(), &ShotServer::aboutToDispatchInstall,
                     &mainController, quietNetworkForApkInstall);
#endif

    // Weather forecast manager (hourly updates, region-aware API selection)
    WeatherManager weatherManager(&sharedNetworkManager);
    weatherManager.setLocationProvider(mainController.locationProvider());

    // DE1 auto-reconnect state — declared early because autoWakeManager and
    // applicationStateChanged lambdas capture these by reference.
    int de1ReconnectAttempt = 0;
    QTimer de1ReconnectTimer;
    de1ReconnectTimer.setSingleShot(true);
    // Tracks "was connected or connecting" for edge-detection in the
    // connectedChanged handler. Updated by connectingChanged so startup
    // failures (connecting→failed, never reached connected) also arm the
    // retry timer — not just mid-session disconnects (connected→disconnected).
    bool de1WasActive = false;

    // Auto-wake manager for scheduled wake-ups
    AutoWakeManager autoWakeManager(settings.autoWake());
    QObject::connect(&autoWakeManager, &AutoWakeManager::wakeRequested,
                     &de1Device, &DE1Device::wakeUp);
    QObject::connect(&autoWakeManager, &AutoWakeManager::wakeRequested,
                     &mainController, &MainController::autoWakeTriggered);
    // Also wake the scale and reconnect DE1 if needed
    QObject::connect(&autoWakeManager, &AutoWakeManager::wakeRequested,
                     [&physicalScale, &bleManager, &settings, &de1Device, &de1ReconnectTimer, &de1ReconnectAttempt]() {
        qDebug() << "AutoWakeManager: Waking scale and reconnecting DE1 if needed";
        if (!de1Device.isConnected() && !de1Device.isConnecting()) {
            // Reset reconnect counter and start fresh retry sequence
            de1ReconnectAttempt = 0;
            if (!de1ReconnectTimer.isActive()) {
                de1ReconnectTimer.start(500);
            }
        }
        if (physicalScale && physicalScale->isConnected()) {
            physicalScale->wake();
        } else if (!settings.scaleAddress().isEmpty()) {
            // Scale disconnected - try to reconnect. DE1 wake is a foreground
            // trigger, so allow the bounded direct-connect fast-path (default).
            QTimer::singleShot(500, &bleManager, [&bleManager]() {
                bleManager.tryDirectConnectToScale();
            });
        }
    });
    autoWakeManager.start();

    // Sensor calibration capture. Declared before `engine` like every other
    // published singleton, so it outlives the bindings that read it (see the
    // LIFETIME note in contextsingletons_qml.h).
    SensorCalibrationController sensorCalibration(&de1Device, &translationManager);
    {
        // The two facts the wizard needs about the loaded profile: its FILENAME
        // (baseProfileName — currentProfileName is a display string that
        // decorates itself once edited) and its final frame's declared holds.
        // That last frame is the one the test profiles end on and the one the
        // user is watching when they read their gauge.
        ProfileManager* pm = mainController.profileManager();
        sensorCalibration.setProfileContextProvider(
            [pm]() -> SensorCalibrationController::ProfileContext {
                SensorCalibrationController::ProfileContext ctx;
                if (!pm) return ctx;
                ctx.filename = pm->baseProfileName();
                const QList<ProfileFrame>& steps = pm->currentProfile().steps();
                if (!steps.isEmpty()) {
                    ctx.holdPressure = steps.last().pressure;
                    ctx.holdTemperature = steps.last().temperature;
                }
                return ctx;
            });
        QObject::connect(pm, &ProfileManager::currentProfileChanged,
                         &sensorCalibration, &SensorCalibrationController::noteProfileChanged);
    }

    // Database backup manager for scheduled daily backups
    DatabaseBackupManager backupManager(&settings, mainController.shotHistory(),
                                       &profileStorage, &screensaverManager);
    mainController.setBackupManager(&backupManager);
    backupManager.setTranslationManager(&translationManager);
    QObject::connect(&backupManager, &DatabaseBackupManager::backupCreated,
                     [](const QString& path) {
        qDebug() << "DatabaseBackupManager: Backup created successfully:" << path;
    });
    QObject::connect(&backupManager, &DatabaseBackupManager::backupFailed,
                     [](const QString& error) {
        qWarning() << "DatabaseBackupManager: Backup failed:" << error;
    });
    QObject::connect(&backupManager, &DatabaseBackupManager::profilesRestored,
                     mainController.profileManager(), &ProfileManager::refreshProfiles);
    QObject::connect(&backupManager, &DatabaseBackupManager::mediaRestored,
                     &screensaverManager, &ScreensaverVideoManager::reloadPersonalMedia);
    backupManager.start();

    checkpoint("Managers wired");

#ifndef Q_OS_IOS
    // Scanning is opt-in for both the DE1 and the scale — see
    // Settings::usbSerialEnabled for what it does and does not gate.
    const auto applyUsbScanning = [&]() {
        if (settings.usbSerialEnabled()) {
            usbManager.startPolling();
            usbScaleManager.startPolling();
        } else {
            usbManager.stopPolling();
            usbScaleManager.stopPolling();
            // Without this a submitted log carries no USB lines at all, and "off"
            // reads exactly like "broken".
            SCALE_INFO_STDERR_TAGGED("USB Scale",
                QStringLiteral("Scanning disabled (Settings > Connections > Scan for USB "
                               "devices). Hotplug and Scan for Devices still work."));
        }
    };
    // Safe on a never-started manager: finishScanProbe() early-returns with no scan
    // pending, so stopPolling() emits nothing.
    applyUsbScanning();
    QObject::connect(&settings, &Settings::usbSerialEnabledChanged, applyUsbScanning);

    // Outside the setting: an idle receiver costs nothing.
    UsbHotplug::start(&usbScaleManager, &usbManager);

    // Hotplug only reports devices attached WHILE the app runs, so one plugged in
    // before launch is otherwise invisible until a replug or a Scan. Ungated: the
    // setting bounds a per-tick cost, and this is one hasDevice() per launch.
    usbScaleManager.onHotplugEvent();
    usbManager.onHotplugEvent();
    QObject::connect(&app, &QCoreApplication::aboutToQuit, []() { UsbHotplug::stop(); });
#endif

    AccessibilityManager accessibilityManager;
    accessibilityManager.setTranslationManager(&translationManager);

    // Steam-coach voice: the coach emits speakRequested only when its own audio
    // setting is on; route it via announceCoaching, which bypasses BOTH
    // accessibility voice gates — the master switch AND the Voice Announcements
    // (ttsEnabled) toggle; that second bypass is load-bearing (see
    // AccessibilityManager::announceCoaching). Do not reroute this through
    // announce()/routeAnnouncement — either re-gates the voice.
    QObject::connect(mainController.liveSteamCoach(), &LiveSteamCoach::speakRequested,
                     &accessibilityManager, &AccessibilityManager::announceCoaching);

    // Now that all managers exist, finish MCP server setup
    mcpServer.setAccessibilityManager(&accessibilityManager);
    mcpServer.registerAllTools();
    mcpServer.registerAllResources();
    mcpServer.connectSseNotifications();

    // Start the remote connector listener now if it was left enabled.
    remoteMcpAccess.refresh();

    // Crash reporter for sending crash reports to api.decenza.coffee
    CrashReporter crashReporter;

#ifdef Q_OS_ANDROID
    // Drop CrashReporter's private QNAM keepalive sockets before APK install.
    // Same rationale as the quietNetworkForApkInstall lambda above; this is
    // a separate connect because crashReporter is constructed after that
    // lambda's call site.
    QObject::connect(mainController.updateChecker(), &UpdateChecker::aboutToDispatchInstall,
                     &crashReporter, &CrashReporter::clearConnectionCache);
    QObject::connect(mainController.shotServer(), &ShotServer::aboutToDispatchInstall,
                     &crashReporter, &CrashReporter::clearConnectionCache);
#endif

    checkpoint("Pre-QML setup done");

    // Same rule, and this one was learned the hard way. The refractometer is
    // wired up ~950 lines below, next to the rest of its BLE handling, but the
    // unique_ptr lives HERE so the device outlives the engine. Declared down
    // there it was destroyed first, and ~QObject's destroyed() reached a live
    // QML binding on BLEManager.refractometerConnected, which dispatched a pure
    // virtual on the half-destroyed object — a SIGBUS on quit. Keep the
    // declaration above `engine`; the QPointer in BLEManager is the second
    // line of defence, not a licence to move this back down.
    std::unique_ptr<RefractometerDevice> refractometer;

    // Hand the previous run's crash log to the singleton QML already uses for crash reporting,
    // replacing the bare "PreviousCrashLog"/"PreviousDebugLogTail" context properties. crashReporter
    // is declared further up and already outlives `engine`, so this needs no hoist of its own.
    crashReporter.setPreviousRun(previousCrashLog, previousDebugLogTail);

    // Hoisted here from ~1500 lines below for that same rule, when it became a QML singleton.
    // A context property is dropped by QML when its object emits destroyed(), so it survived
    // being declared after `engine`; a singleton reached through a static raw pointer has no
    // such mechanism and nothing nulls it, so the declaration has to outlive the engine.
    // Only the DECLARATION moved: the three setters that give it its dependencies stay where
    // they were, because mainController.shotHistory() is not ready this early.
    FlowCalibrationModel flowCalibrationModel;

    // The stable QML identity for whichever scale is live. Declared here for the same lifetime
    // rule as everything else registered as a singleton; the eleven places below that used to
    // re-point the `ScaleDevice` context property now call setTarget() on this.
    ScaleDeviceProxy scaleProxy;
    RefractometerProxy refractometerProxy;
    mainController.setScaleDeviceProxy(&scaleProxy);

    // Hoisted for the same rule, and note it was ALREADY exposed to QML from below the engine —
    // as a context property, which QML drops on destroyed(), so the ordering hazard was papered
    // over rather than absent. Its constructor takes no dependencies (it just blanks the LEDs),
    // so only the DECLARATION moves; setDE1Device()/setDE1Simulator() stay where the simulator
    // exists, ~1750 lines below.
#if (defined(Q_OS_WIN) || defined(Q_OS_MACOS)) && defined(QT_DEBUG) && defined(DECENZA_SIMULATOR)
    GHCSimulator ghcSimulator;
#endif

    // Set up QML engine
    QQmlApplicationEngine engine;
    checkpoint("QML engine created");

    // Auto-connect when DE1 is discovered via BLE
    // Tell BLEManager whether a DE1 connect is actually in flight, so it can hold
    // a scale's direct-connect behind it (two concurrent GATT connects collide on
    // the Android stack). Both signals, because DE1Device emits connectingChanged
    // alone when an attempt STARTS and both when one ends — reading one of them
    // would miss an edge. isConnecting() clears in onTransportConnected(), which
    // runs only after every DE1 notification subscription is confirmed, so the
    // gate brackets connect, discovery and subscribe.
    const auto noteDe1Connecting = [&de1Device, &bleManager]() {
        bleManager.noteDe1Connecting(de1Device.isConnecting());
    };
    QObject::connect(&de1Device, &DE1Device::connectingChanged, &bleManager, noteDe1Connecting);
    QObject::connect(&de1Device, &DE1Device::connectedChanged, &bleManager, noteDe1Connecting);

    QObject::connect(&bleManager, &BLEManager::de1Discovered,
                     &de1Device, [&de1Device, &bleManager, &physicalScale, &settings
#ifndef Q_OS_IOS
                     , &usbManager
#endif
                     ](const QBluetoothDeviceInfo& device) {
#ifndef Q_OS_IOS
        // Don't connect via BLE if already connected via USB
        if (usbManager.isDe1Connected()) {
            bleManager.de1Debug(QStringLiteral("de1Discovered: skipping BLE connect - USB already "
                                               "connected"), QStringLiteral("main"));
            return;
        }
#endif
        if (!de1Device.isConnected() && !de1Device.isConnecting()) {
            de1Device.connectToDevice(device);

            // Save DE1 address for direct wake on next startup
            QString identifier = getDeviceIdentifier(device);
            settings.setMachineAddress(identifier);
            bleManager.setSavedDE1Address(identifier, device.name());

            // Only stop scan if we're not still looking for a scale
            bool lookingForScale = bleManager.hasSavedScale() || bleManager.isScanningForScales();
            if (!lookingForScale || (physicalScale && physicalScale->isConnected())) {
                bleManager.stopScan();
            }
        }
    });

    // No DE1Device::logMessage forwarder. It existed to feed the connections-page
    // DE1 view through BLEManager::de1LogMessage, a signal that reached that window
    // and nothing else — so everything sent through it was missing from every log a
    // user submitted. DE1Device's macros already write each line to the system log
    // carrying [DE1][<source>], which is what the view now reads.

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

    // No USBManager::logMessage forwarder either — same reason. USB_LOG/INFO/WARN
    // already carry [DE1][USB].
#endif

    // Scale auto-reconnect after disconnect: backoff ramp 5s → 30s → 60s, then
    // the 60s tail repeats while the scale stays disconnected — slowing to 5min
    // once it is clear the scale is not coming back this sitting (see below).
    // First retry is quick (5s); the 30s/60s delays exceed BLE's 20s connection
    // timeout so each attempt completes before the next fires. We never give up
    // permanently (matches de1app): a scale powered back on hours later is
    // reconnected automatically because each retry runs a ~15s scan that
    // connects as soon as it sees the saved scale advertising, so it is picked
    // up within a retry cycle (#1207).
    int scaleReconnectAttempt = 0;
    QTimer scaleReconnectTimer;
    scaleReconnectTimer.setSingleShot(true);
    const std::vector<int> reconnectDelays = {5000, 30000, 60000};

    // …but the 60 s tail runs at that cadence for a bounded number of cycles,
    // then drops to 5 minutes. kScaleFastTailAttempts counts TOTAL attempts, so
    // with a 3-step ramp the 60 s branch runs 8 times and the crossing lands at
    // 5 + 30 + 60×8 ≈ 8.6 min. A saved scale still absent by then is not "about
    // to appear" — it is off, out of range, or (the case that prompted this) a
    // WiFi scale whose host is not on this network at all, failing every dial
    // with HostNotFoundError. Each cycle is not free: when the WiFi dial fails
    // and the cached IP is stale, the cycle also runs an mDNS resolve and then
    // the ~15 s BLE-scan fallback (beginWifiFallbackToBleScan) — once a minute,
    // for as long as the app is open. MQTT next door already ramps 5→60 s and
    // then holds at 15 min for exactly this reason (see MAX_FAST_RECONNECT_-
    // ATTEMPTS in mqttclient.h, also 10); this is the same shape, kept much
    // shorter because a scale that IS powered back on should still be picked up
    // promptly.
    //
    // Nothing is given up by slowing down: every event that means "a scale
    // might be here now" restarts the ramp at 5 s with the counter cleared —
    // app resume, screensaver exit, and DE1 wake. A user-initiated scan and a
    // successful connect also clear the counter, but they STOP the ladder
    // rather than restarting it, which is correct for both. The worst case is a
    // scale switched on while the app sits untouched in the foreground, which
    // waits up to 5 min instead of up to 1.
    //
    // Each of those three restart sites used to be gated on the reconnect timer
    // being idle. That gate silently voided all three: the timer is single-shot
    // and re-armed at the end of every tick, so it is ALWAYS active while the
    // ladder runs, which is precisely when a restart is wanted. Do not
    // reintroduce it — QTimer::start() on an active single-shot timer just
    // restarts it, so no guard is needed.
    const int kScaleFastTailAttempts = 10;
    const int kScaleSlowTailMs = 300000;  // 5 min

    // When Settings.keepScaleOn is false we deliberately disconnect the scale
    // on DE1 sleep. The connectedChanged handler below normally schedules an
    // auto-reconnect 5 s after any disconnect — this flag suppresses that for
    // our deliberate path. Cleared on DE1 wake (Idle), app resume, and user-
    // initiated scan so normal reconnect behaviour resumes.
    bool scaleAutoReconnectSuppressed = false;

    // Tracks whether disableLcd() was called on a BT scale during DE1 sleep
    // and a wake() to restore the LCD hasn't run yet. Set in the DE1 sleep
    // handler (BT keepScaleOn=true path only — WiFi's onConnected sends
    // "display on" on its own reconnect handshake, so no separate restore is
    // needed). Cleared whenever wake() is called: from the phaseChanged wake
    // handler on the happy path (scale stayed connected through sleep), or
    // from connectedChanged when a BT scale reconnects post-wake after having
    // dropped mid-sleep (or after the wake handler's fallthrough couldn't
    // act). Independent from scaleAutoReconnectSuppressed because the two
    // concerns (reconnect arming vs. LCD restore) decouple under app-resume
    // and user-initiated-scan paths that clear the suppression flag.
    bool scaleLcdRestorePending = false;

    // DE1-phase tracking flags (declared here so the disconnectScaleRequested
    // handler below can clear them; the phaseChanged lambda that owns them
    // lives much further down — see "Manage scale power state…" block).
    //   de1EverAwake: suppress Sleep reaction on initial connect (DE1's
    //     default BLE state is Sleep, so MachineState transitions
    //     Disconnected→Sleep before the real state arrives).
    //   wasInSleep: tracks whether the previous phase was Sleep, so the wake
    //     actions fire on the very first non-Sleep transition (the DE1 typically
    //     wakes into Phase::Heating or Phase::Ready, not Phase::Idle).
    bool de1EverAwake = false;
    bool wasInSleep = false;

    // R2 refractometer auto-reconnect: same persistent backoff as the scale
    // (5s → 30s → 60s, then 60s forever). The R2 is only used to capture TDS/EY
    // on the post-shot review page, so — unlike the scale — this tick is scoped
    // to that page's "hunt": while the hunt is active it keeps trying whenever
    // the R2 is disconnected and an address is saved; off the review page the
    // tick self-stops (see the isRefractometerHunt() guard in its handler) and
    // is re-armed when the hunt turns back on. Shares reconnectDelays with the
    // scale (whose reconnect is independent and always-on).
    int refractometerReconnectAttempt = 0;
    QTimer refractometerReconnectTimer;
    refractometerReconnectTimer.setSingleShot(true);

    // Lifetime guard for the signal handlers below that capture main()'s locals
    // by reference.
    //
    // Stack objects destruct in reverse declaration order, so everything declared
    // after `engine` (the reconnect state just above) is destroyed BEFORE
    // `engine` is. The senders those handlers hang off
    // — app, bleManager, machineState, screensaverManager, the scale — are all
    // declared above `engine` and so outlive the very locals their handlers write
    // to. A signal emitted during teardown then runs a lambda over dead stack.
    //
    // Not theoretical: ~QQmlApplicationEngine drives QML context destruction,
    // which re-enters C++ setters (BLEManager::setRefractometerHunt is one), and
    // the resulting emission ran a handler that assigned to an already-destroyed
    // stack int — an ASan use-after-scope abort on quit.
    //
    // Passing this as each connection's context object makes Qt sever them all
    // when it dies. The explicit reset() after app.exec() is what does the work
    // and must not be dropped: it runs ahead of every local's destructor, whereas
    // this unique_ptr's own destructor fires at THIS declaration point and so
    // would already be too late for anything declared below it.
    //
    // Handlers whose sender IS one of these locals (the reconnect timers) need no
    // guard — the connection already dies with the sender.
    auto handlerScope = std::make_unique<QObject>();

    QObject::connect(&scaleReconnectTimer, &QTimer::timeout,
                     [&bleManager, &settings, &scaleReconnectAttempt, &scaleReconnectTimer,
                      &reconnectDelays]() {   // the two const tail constants need no capture
        if (settings.scaleAddress().isEmpty()) {
            // scaleReconnectTimer is single-shot (see its setSingleShot(true) at
            // construction), so this return does not re-arm — the ladder is
            // permanently dead from this point on. Worth a line above DEBUG: it was
            // a bare qDebug, invisible to a [Scale] search, so "why did it stop
            // trying to reconnect my scale" had no answer in a submitted log.
            bleManager.scaleInfo(QStringLiteral(
                "Scale reconnect: no saved scale address, stopping retries"),
                QStringLiteral("main"));
            return;
        }
        // USB scales are owned by UsbScaleManager and reconnect via its
        // usbScaleAvailable handler — NOT this BLE/WiFi direct-wake timer.
        // tryDirectConnectToScale() early-returns for "usb:" addresses, so
        // re-arming here would spin forever (and resetScaleConnectionState()
        // below would needlessly stop the BLE connection timer each tick).
        // Stop the timer when the saved scale is USB.
        if (!scaleAddressIsLadderDialable(settings.scaleAddress())) {
            // Also a ladder-ending return on a single-shot timer — same reasoning
            // as above.
            bleManager.scaleInfo(QStringLiteral(
                "Scale reconnect: saved scale is not dialable by this ladder "
                "(USB is handled by UsbScaleManager; sim: is the simulator's "
                "synthetic entry) — stopping retries"), QStringLiteral("main"));
            return;
        }
        // One line, INFO while the ramp walks and DEBUG on the endless 60 s tail.
        // This was a pair: an unmarked `qDebug() << "Scale reconnect: attempt" ...`
        // for every attempt plus a marked appendScaleLog for the bounded ramp only.
        // The split existed because the two sinks had different needs; with one
        // sink, the tier does that job and the tail stops shouting.
        {
            const QString attemptMsg =
                QStringLiteral("Auto-reconnect attempt %1").arg(scaleReconnectAttempt + 1);
            if (scaleReconnectAttempt < static_cast<int>(reconnectDelays.size()))
                bleManager.scaleInfo(attemptMsg, QStringLiteral("main"));
            else
                bleManager.scaleDebug(attemptMsg, QStringLiteral("main"));
        }
        bleManager.resetScaleConnectionState();
        // Background reconnect: scan only, never a parked direct-connect. A
        // direct connectToDevice() to an absent scale holds the Android BLE
        // stack in Connecting for ~30s every cycle and starves the DE1 link
        // (issue #1303). The saved scale auto-connects when seen in a scan.
        bleManager.tryDirectConnectToScale(/*allowDirectConnect=*/false);
        scaleReconnectAttempt++;
        // Persistent reconnect: walk the ramp, hold on the last (60s) delay for
        // kScaleFastTailAttempts cycles, then hold on the 5-minute delay
        // forever. Stops naturally when the scale connects (connectedChanged),
        // the user forgets it (scaleAddress empty, above), or the user scans for
        // a different scale.
        if (scaleReconnectAttempt < static_cast<int>(reconnectDelays.size())) {
            scaleReconnectTimer.start(reconnectDelays[scaleReconnectAttempt]);
        } else if (scaleReconnectAttempt < kScaleFastTailAttempts) {
            scaleReconnectTimer.start(reconnectDelays.back());
        } else {
            // Announce the one crossing, not every slow cycle — the 5-minute
            // tail runs forever and the scale log is a 1000-entry ring buffer.
            // `==` rather than `>=` is what makes it one-shot, and it re-arms by
            // itself when a genuine reset event clears the counter.
            if (scaleReconnectAttempt == kScaleFastTailAttempts) {
                const QString msg =
                    QString("Scale still absent after %1 attempts — slowing retries to every %2 min")
                        .arg(scaleReconnectAttempt).arg(kScaleSlowTailMs / 60000);
                // qWarning, matching MQTT's equivalent crossing: this is the line
                // that explains a log which otherwise looks like the reconnect
                // died, and WARN is what makes it findable in a submitted log.
                bleManager.scaleWarn(msg, QStringLiteral("main"));
            }
            scaleReconnectTimer.start(kScaleSlowTailMs);
        }
    });

    // Arm the scale retry timer when a startup direct-connect attempt times out.
    // Without this, a startup connection timeout (scale asleep / not advertising, 20 s)
    // leaves the scale disconnected until the user manually reopens the app —
    // same root cause as the DE1 reconnect bug fixed in a0bade6f. flowScaleFallback
    // is guarded by m_flowScaleFallbackEmitted so it fires at most once per session
    // (until the scale connects or the user clears it — resetScaleConnectionState()
    // deliberately does not reset this guard); the retry loop above handles
    // subsequent timeout failures itself.
    QObject::connect(&bleManager, &BLEManager::flowScaleFallback, handlerScope.get(),
                     [&settings, &bleManager, &scaleReconnectTimer, &scaleReconnectAttempt,
                      &reconnectDelays, &scaleAutoReconnectSuppressed]() {
        if (settings.scaleAddress().isEmpty()) {
            qDebug() << "Scale reconnect (startup): no saved address, skipping";
            return;
        }
        // USB scales reconnect via UsbScaleManager (usbScaleAvailable), not this
        // BLE/WiFi timer. Arming it would fire once and self-terminate at the
        // timeout guard — skip arming entirely.
        if (!scaleAddressIsLadderDialable(settings.scaleAddress())) {
            return;
        }
        if (scaleAutoReconnectSuppressed) {
            qDebug() << "Scale reconnect (startup): suppressed (deliberate DE1-sleep disconnect), skipping";
            return;
        }
        if (scaleReconnectTimer.isActive()) {
            qDebug() << "Scale reconnect (startup): timer already active, skipping";
            return;
        }
        scaleReconnectAttempt = 0;
        scaleReconnectTimer.start(reconnectDelays[0]);
        bleManager.scaleInfo(QStringLiteral("Scheduling reconnect in %1 s (startup failure)")
                                 .arg(reconnectDelays[0] / 1000),
                             QStringLiteral("main"));
    });

    // Re-arm the scale reconnect when the simulated scale is switched OFF.
    // setScaleSimulated(true) stops the connection timer and drops the physical
    // scale; nothing restarts either, so without this the real scale stays
    // stranded until an app restart or a manual rescan — with no user-visible
    // reason. Mirrors the R2 disabledChanged re-arm further down.
    QObject::connect(&bleManager, &BLEManager::scaleSimulatedChanged,
                     handlerScope.get(), [&bleManager, &settings, &scaleReconnectTimer,
                                   &scaleReconnectAttempt, &reconnectDelays,
                                   &scaleAutoReconnectSuppressed]() {
        if (bleManager.isScaleSimulated())
            return;  // rising edge — the teardown in setScaleSimulated is correct
        // "sim:" is excluded because the simulator promotes its own synthetic
        // address to primary when no real scale was ever paired — arming the
        // ladder against it dials nonsense and ends in a "No Scale Found"
        // dialog every 60 s. tryDirectConnectToScale guards this too; the check
        // is repeated here so the ladder isn't started only to no-op.
        if (!scaleAddressIsLadderDialable(settings.scaleAddress())
            || scaleAutoReconnectSuppressed
            || scaleReconnectTimer.isActive())
            return;
        scaleReconnectAttempt = 0;
        scaleReconnectTimer.start(reconnectDelays[0]);
        bleManager.scaleInfo(
            QStringLiteral("Simulated scale switched off — resuming reconnect in %1 s")
                .arg(reconnectDelays[0] / 1000),
            QStringLiteral("main"));
    });

    // Re-arm the reconnect ladder on EVERY scale-connection failure, not just
    // the first one. flowScaleFallback above is gated to fire once per saved-
    // scale cycle (so the "No Scale Found" dialog doesn't re-pop on every
    // retry), but the retry timer itself must survive the WiFi→BLE-fallback
    // failure case: the scale-type change in that path stops scaleReconnectTimer
    // (see "Scale reconnect: timer stopped due to scale type change" below),
    // and without this signal there was no path to start it back up. The
    // timer's own slot self-perpetuates once running, so we just need to start
    // it once per failure cycle — the slot will keep it going. Uses the long-
    // tail delay (60 s) because the immediate failure has already happened;
    // hammering harder would just churn the WiFi radio.
    QObject::connect(&bleManager, &BLEManager::scaleRetryNeeded, handlerScope.get(),
                     [&settings, &bleManager, &scaleReconnectTimer, &scaleReconnectAttempt,
                      &reconnectDelays, &scaleAutoReconnectSuppressed]() {
        if (!scaleAddressIsLadderDialable(settings.scaleAddress())) return;
        if (scaleAutoReconnectSuppressed) return;
        if (scaleReconnectTimer.isActive()) return;
        // Move the counter UP to the end of the ramp, never down. It doubles as
        // the slow-tail budget (see kScaleFastTailAttempts), and a connection
        // failure is not evidence the scale is coming back — so a plain
        // assignment here let any path that idles the timer (notably the
        // scale-type change in the scaleDiscovered handler) reset the budget to
        // 2 and make the 5-min tail unreachable.
        const int rampTailIndex = static_cast<int>(reconnectDelays.size()) - 1;
        if (scaleReconnectAttempt < rampTailIndex)
            scaleReconnectAttempt = rampTailIndex;
        const int retryDelayMs = scaleReconnectAttempt >= kScaleFastTailAttempts
                                     ? kScaleSlowTailMs : reconnectDelays.back();
        scaleReconnectTimer.start(retryDelayMs);
        bleManager.scaleInfo(QStringLiteral("Scheduling reconnect in %1 s (retry after failure)")
                                 .arg(retryDelayMs / 1000),
                             QStringLiteral("main"));
    });

    // Every "a scale might be here now" event restarts the ladder through here.
    // The gates were previously spelled out at each site and had drifted: four
    // tested the `usb:` prefix instead of scaleAddressIsLadderDialable(), which
    // also excludes the simulator's `sim:` entry, so they armed a ladder the
    // first tick permanently terminates. Callers that mean "the user is back"
    // clear scaleAutoReconnectSuppressed before calling; that is a judgement
    // about intent, not a gate, so it stays with them.
    QObject::connect(&bleManager, &BLEManager::scaleReconnectRampRestartRequested,
                     handlerScope.get(),
                     [&settings, &physicalScale, &bleManager, &scaleReconnectTimer,
                      &scaleReconnectAttempt, &reconnectDelays,
                      &scaleAutoReconnectSuppressed](const QString& reason,
                                                     int firstDelayMs) {
        if (physicalScale && physicalScale->isConnected()) return;
        if (!scaleAddressIsLadderDialable(settings.scaleAddress())) return;
        if (scaleAutoReconnectSuppressed) return;
        const int delayMs = firstDelayMs >= 0 ? firstDelayMs : reconnectDelays[0];
        scaleReconnectAttempt = 0;
        scaleReconnectTimer.start(delayMs);
        bleManager.scaleInfo(QStringLiteral("%1 — restarting scale reconnect ramp, first retry in %2 ms")
                                 .arg(reason).arg(delayMs),
                             QStringLiteral("main"));
    });

    // === Proactive switch-back to the WiFi primary scale ===
    // When the saved primary is a WiFi scale but we're currently on the BLE
    // backup (the WiFi->BLE fallback connected after WiFi was unreachable),
    // periodically check — only while the machine is idle (idle page, never
    // mid-shot) — whether the WiFi scale is reachable again, and hop back if so.
    // The check (BLEManager::probeWifiPrimaryReachable → a WebSocket HDS-identity
    // probe against ws://<cached-ip>/snapshot, requiring a valid HDS frame
    // within ~3.5 s) is non-disruptive: it never touches the live BLE link, so
    // a failed check leaves the working backup untouched. A bare TCP-open on
    // port 80 was the old check; it false-positived against any LAN device on
    // 80 and triggered the WiFi↔BLE thrash in #1281. This is genuine periodic
    // polling (for an external availability change), not a timer-as-guard.
    QTimer wifiPreferTimer;
    constexpr int kWifiPreferIntervalMs = 30000;  // re-check every 30 s while armed

    // Armed only on the WiFi-primary / BLE-backup combination AND while the
    // machine is in a non-brewing phase. "Non-brewing" = Disconnected (no DE1,
    // i.e. scale-only debugging), Sleep, Idle, Ready; ANY other phase (Heating,
    // EspressoPreheating, Preinfusion, Pouring, Ending, Steaming, HotWater,
    // Flushing, Refill, Descaling, Cleaning) blocks the switch so the scale is
    // never disrupted mid-operation.
    auto onWifiBackupAndIdle = [&settings, &physicalScale, &machineState]() -> bool {
        if (!settings.scaleAddress().startsWith(QStringLiteral("wifi:"), Qt::CaseInsensitive))
            return false;                                  // primary isn't WiFi
        if (!physicalScale || !physicalScale->isConnected())
            return false;                                  // nothing connected → reconnect machinery owns it
        if (physicalScale->type() == ScaleTypeIds::scaleTypeId(ScaleType::DecentScaleWifi))
            return false;                                  // already on the WiFi primary
        switch (machineState.phase()) {
        case MachineState::Phase::Disconnected:
        case MachineState::Phase::Sleep:
        case MachineState::Phase::Idle:
        case MachineState::Phase::Ready:
            return true;
        default:
            return false;                                  // brewing / active — don't switch
        }
    };

    QObject::connect(&wifiPreferTimer, &QTimer::timeout,
                     [&settings, &bleManager, onWifiBackupAndIdle]() {
        if (!onWifiBackupAndIdle()) return;
        const QString hostname = settings.scaleAddress().mid(QStringLiteral("wifi:").size());
        const QString cachedIp = settings.network()->wifiScaleIp(hostname);
        if (cachedIp.isEmpty()) return;  // no cheap probe target (mDNS is unreliable here) — skip this cycle
        bleManager.probeWifiPrimaryReachable(cachedIp);
    });
    wifiPreferTimer.start(kWifiPreferIntervalMs);

    QObject::connect(&bleManager, &BLEManager::wifiPrimaryReachable,
                     [&bleManager, onWifiBackupAndIdle](bool reachable) {
        if (!reachable) return;
        // Re-validate: up to ~3 s elapsed during the probe, so state may have
        // changed (a shot started, the user picked a scale, WiFi already back…).
        if (!onWifiBackupAndIdle()) return;
        bleManager.switchToWifiPrimary();
    });

    // DE1 auto-reconnect after disconnect. Matches de1app behaviour: on Android it
    // retries essentially forever (99999999 attempts) because the DE1 may be in deep
    // sleep and take a while to become reachable. We use backoff: 5s, 30s, then 60s
    // repeated for the first 12 attempts (5s + 30s + 10×60s ≈ 10.5 min), then drop to
    // a slow background retry that continues indefinitely.
    //
    // We deliberately do NOT give up permanently (#1309): the original code stopped
    // after 12 attempts, which left a DE1 unreachable for ~22h in a real log — a
    // screensaver/screen-tap wake fired one connect but couldn't restart the dead
    // ladder. A slow forever-retry costs one connect attempt per 5 min (negligible)
    // and (a) reconnects a DE1 that was simply powered off for >10 min and (b) keeps
    // the BLE-stack-wedge detector fed so its adapter power-cycle stays viable.
    constexpr int kDE1MaxReconnectAttempts = 12;     // 5s + 30s + 10*60s = ~10.5 min of fast retries
    constexpr int kDE1SlowReconnectMs = 5 * 60 * 1000;  // then every 5 min, forever

    QObject::connect(&de1ReconnectTimer, &QTimer::timeout,
                     [&bleManager, &de1Device, &settings, &de1ReconnectAttempt, &de1ReconnectTimer]() {
        if (settings.machineAddress().isEmpty()) {
            // de1ReconnectTimer is single-shot, so this return does not re-arm —
            // the ladder is permanently dead from here. Was a bare qDebug,
            // invisible to a [DE1] search; the scale ladder's equivalent lines
            // were converted, this one was missed.
            bleManager.de1Info(QStringLiteral(
                "DE1 reconnect: no saved DE1 address, stopping retries"),
                QStringLiteral("main"));
            return;
        }
        if (de1Device.isConnected() || de1Device.isConnecting()) {
            bleManager.de1Info(QStringLiteral(
                "DE1 reconnect: already connected/connecting, stopping retries"),
                QStringLiteral("main"));
            return;
        }
        // Clamp the counter at the cap so it doesn't grow without bound across
        // days of slow retries; once capped we stay on the slow tier. Track
        // whether THIS tick is the one that reached the cap — unlike the scale
        // ladder's counter (unclamped, so `== kScaleFastTailAttempts` is
        // naturally one-shot), this one stops moving once capped, so an
        // uncorrected `==` check below would be true on every slow tick forever
        // rather than once.
        const bool justHitCap = de1ReconnectAttempt < kDE1MaxReconnectAttempts
                                 && ++de1ReconnectAttempt == kDE1MaxReconnectAttempts;
        bleManager.de1Debug(QStringLiteral("DE1 reconnect: attempt %1 of %2")
                                 .arg(de1ReconnectAttempt).arg(kDE1MaxReconnectAttempts),
                             QStringLiteral("main"));
        bleManager.tryDirectConnectToDE1();

        if (de1ReconnectAttempt < kDE1MaxReconnectAttempts) {
            // 30s after first attempt, 60s for all subsequent
            // (the initial 5s delay before attempt 1 is set by connectedChanged)
            int delay = de1ReconnectAttempt == 1 ? 30000 : 60000;
            de1ReconnectTimer.start(delay);
        } else {
            if (justHitCap) {
                // Announce the one crossing, not every slow cycle — mirrors the
                // scale ladder's identical crossing (main.cpp,
                // scaleReconnectTimer handler). WARN is what makes the crossing
                // findable in a submitted log — without it, a machine gone for
                // 10+ minutes reads as a ladder that silently died.
                bleManager.de1Warn(QStringLiteral(
                    "DE1 still absent after %1 attempts — slowing retries to every %2 min")
                        .arg(de1ReconnectAttempt).arg(kDE1SlowReconnectMs / 60000),
                    QStringLiteral("main"));
            }
            de1ReconnectTimer.start(kDE1SlowReconnectMs);
        }
    });

    // When the DE1 starts connecting, mark it as active so that if the
    // attempt fails before reaching connected, connectedChanged will still
    // recognise the inactive transition and arm the retry timer.
    QObject::connect(&de1Device, &DE1Device::connectingChanged,
                     [&de1Device, &de1WasActive]() {
        if (de1Device.isConnecting()) de1WasActive = true;
    });

    // Feed DE1 controller faults to the BLE-stack-wedge detector (#1309). This
    // is separate from the dual-HIGH scale-transport wiring below (which only
    // exists when a BLE scale is present) — the wedge detector must hear faults
    // regardless of whether a scale transport was ever created.
    QObject::connect(&de1Device, &DE1Device::de1LinkFault,
                     &bleManager, &BLEManager::onDe1LinkFault);

    // Surface DE1 BLE errors to the UI. DE1Device::errorOccurred had no consumer,
    // so DE1 connection problems (incl. the "try toggling Bluetooth off/on" hint)
    // never reached the user — only scale/scan errors did. onDe1Error debounces
    // so the reconnect ladder doesn't re-pop the same dialog. (#1309)
    QObject::connect(&de1Device, &DE1Device::errorOccurred,
                     &bleManager, &BLEManager::onDe1Error);

    // After an automatic adapter power-cycle clears a wedged stack, reset the
    // DE1 reconnect budget and kick a fresh attempt immediately — mirrors the
    // AutoWake re-arm path. Without this the slow-tier timer would wait up to
    // 5 min before retrying a stack that's now healthy. (#1309)
    QObject::connect(&bleManager, &BLEManager::bleStackRecovered,
                     [&de1Device, &de1ReconnectTimer, &de1ReconnectAttempt]() {
        if (de1Device.isConnected() || de1Device.isConnecting()) return;
        de1ReconnectAttempt = 0;
        de1ReconnectTimer.start(500);
        qDebug() << "DE1 reconnect: BLE stack recovered — restarting reconnect ladder (#1309)";
    });

    // When DE1 connects or disconnects, manage reconnect timer.
    //
    // connectedChanged() can fire multiple times while the device is already
    // in the disconnected state — DE1Device::disconnect() emits it
    // unconditionally, and our reconnect path calls disconnect() on the old
    // transport before spinning up a new one. Without edge-tracking, every
    // spurious emission would reset de1ReconnectAttempt=0 and re-arm the 5 s
    // timer, scrambling the backoff schedule. de1WasActive is set true by
    // connectingChanged when a connection attempt starts and reset to false
    // here on each active→inactive transition, so startup failures
    // (connecting→failed without ever reaching connected) also arm the retry
    // timer while spurious inactive→inactive emissions are still suppressed.
    QObject::connect(&de1Device, &DE1Device::connectedChanged,
                     [&de1Device, &de1ReconnectTimer, &de1ReconnectAttempt, &settings, &de1WasActive, &bleManager
#ifndef Q_OS_IOS
                     , &usbManager
#endif
                     ]() {
        const bool isConnected = de1Device.isConnected();
        const bool isActive = isConnected || de1Device.isConnecting();
        // Feed the BLE-stack-wedge detector the DE1 link state (#1309).
        bleManager.noteDe1Connected(isConnected);
        if (!de1WasActive && !isActive) {
            return;  // Spurious inactive→inactive emission — ignore
        }
        const bool wasActive = de1WasActive;
        de1WasActive = isActive;

        if (isConnected) {
            // Just transitioned to connected: stop any pending reconnect attempts.
            de1ReconnectTimer.stop();
            de1ReconnectAttempt = 0;
        } else if (wasActive) {
            // Was active (connecting or connected), now neither: start
            // auto-reconnect if we have a saved address.
#ifndef Q_OS_IOS
            if (usbManager.isDe1Connected()) {
                // Don't try BLE reconnect if USB is handling the DE1
                return;
            }
#endif
            if (settings.machineAddress().isEmpty()) {
                qDebug() << "DE1 reconnect: no saved address — skipping auto-reconnect";
            } else if (!de1ReconnectTimer.isActive()) {
                // Distinguish a fresh disconnect (attempt counter is 0) from a
                // mid-schedule retry attempt that failed (counter > 0). Resetting
                // unconditionally meant every failed retry restarted the 5 s
                // schedule, so the backoff never escalated past attempt 1 — and
                // the "retries exhausted" branch was dead code because the
                // counter never reached the cap (see issue #1309).
                //
                // After the fix this branch is the primary scheduler for every
                // retry after the first: on each failed attempt the timer-
                // callback's "already connecting" early-return (line 1430-1432)
                // bails without rescheduling, then a later connecting→disconnected
                // emission lands here while the timer is idle and advances the
                // schedule to the next backoff step.
                if (de1ReconnectAttempt == 0) {
                    de1ReconnectTimer.start(5000);  // Fresh disconnect — first retry after 5s
                    qDebug() << "DE1 reconnect: scheduled first retry in 5000 ms";
                } else if (de1ReconnectAttempt < kDE1MaxReconnectAttempts) {
                    const int delay = de1ReconnectAttempt == 1 ? 30000 : 60000;
                    de1ReconnectTimer.start(delay);
                    qDebug() << "DE1 reconnect: attempt" << de1ReconnectAttempt
                             << "failed, next retry in" << delay << "ms";
                } else {
                    // Don't give up permanently (#1309) — fall to the slow tier.
                    de1ReconnectTimer.start(kDE1SlowReconnectMs);
                    qDebug() << "DE1 reconnect: fast retries exhausted — slow background retry in"
                             << kDE1SlowReconnectMs << "ms";
                }
            }
        }
    });

    // Connect to any supported scale when discovered
    QObject::connect(&bleManager, &BLEManager::scaleDiscovered, handlerScope.get(),
                     [&physicalScale, &flowScale, &machineState, &mainController, &bleManager, &settings, &timingController, &de1Device, &weightProcessor, &scaleProxy, &scaleReconnectTimer, &scaleReconnectAttempt, &scaleAutoReconnectSuppressed, &scaleLcdRestorePending
                     // By value: this lambda outlives nothing, but the scale
                     // connection it makes below needs the same lifetime guard.
                     , handlerScopePtr = handlerScope.get()
#ifndef Q_OS_IOS
                     , &usbScaleManager
#endif
                     ](const QBluetoothDeviceInfo& device, const QString& type) {
        // A fresh mDNS resolution BLEManager just handed us (see BLEManager::
        // pendingWifiResolvedIp()) is passed straight to connectToHost() as its
        // preferredIp — dialed first, but never written to the persisted IP
        // cache. Keeping unverified resolutions out of the cache is what stops a
        // stale scan-time IP from clobbering a fresher one an actual connection
        // already persisted; the cache is now written only by verified connects
        // (DecentScaleWifi::onRecognizedAsHds) and eviction.
#ifndef Q_OS_IOS
        // Tear down an active USB scale FIRST (before touching physicalScale).
        // The single-scale invariant covers BLE/WiFi via physicalScale, but the
        // USB scale lives in UsbScaleManager — without this, switching from a
        // connected USB scale to BLE/WiFi would leave BOTH feeding weight into
        // WeightProcessor/MainController. disconnectScale() keeps the USB scale
        // AVAILABLE (it's still plugged in) — we're switching away, not losing it.
        if (usbScaleManager.scale()) {
            QObject::disconnect(usbScaleManager.scale(), &ScaleDevice::weightChanged,
                                &mainController, &MainController::onScaleWeightChanged);
            QObject::disconnect(usbScaleManager.scale(), &ScaleDevice::weightSampleReceived,
                                &weightProcessor, &WeightProcessor::processWeight);
            usbScaleManager.disconnectScale();
        }
#endif
        // Single-scale invariant: at most one physical scale is connected at a
        // time (a different scale type replaces the old one below, never runs
        // alongside it). This caps concurrent forced-HIGH BLE links at two —
        // DE1 + scale — the proven-good #1097 baseline. Connecting a second
        // scale simultaneously would make it a third HIGH link and reintroduce
        // the GATT-scheduler contention that tears the weakest link down (the
        // refractometer fix relies on this same 2-link ceiling). If
        // simultaneous multi-scale is ever added, the non-primary link must
        // stay BALANCED / unmanaged like the refractometer transport.
        if (physicalScale && physicalScale->isConnected()) {
            return;
        }

        // Only stop scan if DE1 is already connected/connecting
        if (de1Device.isConnected() || de1Device.isConnecting()) {
            bleManager.stopScan();
        }

        // If we already have a scale object, check if it's the same type
        if (physicalScale) {
            // Compare types via enum to handle format differences (e.g., "decent" vs "Decent Scale")
            if (ScaleFactory::resolveScaleType(physicalScale->type()) != ScaleFactory::resolveScaleType(type)) {
                qDebug() << "Scale type changed from" << physicalScale->type() << "to" << type << "- creating new scale";
                // IMPORTANT: Clear all references before deleting the scale to prevent dangling pointers
                machineState.setScale(&flowScale);  // Switch to FlowScale first
                timingController.setScale(&flowScale);
                // Reconnect FlowScale to WeightProcessor temporarily
                QObject::connect(&flowScale, &ScaleDevice::weightSampleReceived,
                                 &weightProcessor, &WeightProcessor::processWeight);
                bleManager.setScaleDevice(nullptr);  // Clear BLEManager's reference
                physicalScale.reset();  // Now safe to delete old scale
                if (scaleReconnectTimer.isActive()) {
                    bleManager.scaleInfo(QStringLiteral("Reconnect stopped (scale type changed)"),
                                         QStringLiteral("main"));
                }
                scaleReconnectTimer.stop();
                // NOTE: scaleReconnectAttempt is deliberately NOT cleared here.
                // A type change is not evidence the scale is coming back, and
                // this handler runs SYNCHRONOUSLY inside a reconnect tick —
                // BLEManager::tryDirectConnectToScale() emits scaleDiscovered({},
                // "decent-wifi") directly (blemanager.cpp), before the tick
                // increments the counter. With a wifi: primary and a BLE Decent
                // scale in range that never completes a connect, the type
                // alternates "decent-wifi" ↔ "decent" every cycle, so clearing
                // here pinned the counter near the bottom of the ramp and the
                // 5-min tail was unreachable (and the one-shot crossing log
                // could re-fire). The user-driven cases that DO warrant a clear
                // go through disconnectScaleRequested, which clears it there.
            } else {
                // Re-wire to use physical scale
                machineState.setScale(physicalScale.get());
                timingController.setScale(physicalScale.get());
                scaleProxy.setTarget(physicalScale.get());
                if (type == ScaleTypeIds::scaleTypeId(ScaleType::DecentScaleWifi)) {
                    if (auto* wifi = qobject_cast<DecentScaleWifi*>(physicalScale.get())) {
                        // (Re-wire each time — cheap, and ensures the callbacks
                        // reference the live Settings instance.)
                        wireWifiScaleDriver(wifi, settings, bleManager);
                        // If BLEManager just resolved this hostname (a scan
                        // selection), hand the IP to connectToHost() as its
                        // preferredIp so it dials the known IP directly instead
                        // of asking Qt's resolver to re-resolve ".local".
                        // Use the endpoint the scale advertised over DNS-SD
                        // rather than assuming :80/snapshot (defaults to that
                        // when discovery had no TXT data).
                        wifi->setEndpoint(bleManager.pendingWifiPort(),
                                          bleManager.pendingWifiPath());
                        wifi->connectToHost(bleManager.pendingWifiHostname(),
                                            bleManager.pendingWifiResolvedIp());
                    }
                } else {
                    physicalScale->connectToDevice(device);
                }
                return;
            }
        }

        // Create new scale object
        physicalScale = ScaleFactory::createScale(device, type);
        if (!physicalScale) {
            qWarning() << "Failed to create scale for type:" << type;
            return;
        }

        // Save scale to known scales and set as primary. For WiFi entries the
        // identifier is the prefixed hostname; for BLE it's the MAC/UUID.
        const bool isWifi = (type == ScaleTypeIds::scaleTypeId(ScaleType::DecentScaleWifi));
        const QString hostname = isWifi ? bleManager.pendingWifiHostname() : QString();
        const QString deviceId = isWifi ? (QStringLiteral("wifi:") + hostname)
                                         : getDeviceIdentifier(device);
        // Hostname-derived label rather than a generic one — with two WiFi
        // scales paired, one identical name for both makes Known Devices
        // useless. See pendingWifiDisplayName() for why the DNS-SD instance
        // name is deliberately NOT what this is built from.
        const QString displayName = isWifi ? bleManager.pendingWifiDisplayName()
                                            : device.name();
        // Manual "Add WiFi Scale" entries DEFER persistence until the WS
        // endpoint actually validates as an HDS scale. Without this, a typo or
        // a random LAN host (e.g. the user typing their router's IP) would be
        // silently saved as the primary, then dialed on every reconnect /
        // proactive switch-back cycle. The commit happens when
        // DecentScaleWifi::recognizedAsHds fires (first valid HDS frame); if
        // BLEManager's connection timer trips first, manualWifiValidationFailed
        // is emitted to the QML layer instead and nothing is persisted. (#1281)
        const bool deferPersistence = isWifi && bleManager.isManualWifiConnect();
        // BUT preserve the user's chosen primary when this connect is a
        // temporary WiFi→BLE fallback: BLEManager's m_wifiFallbackToBleActive
        // is true only between the WiFi-timeout fallback trigger and the next
        // successful connect. In that window we connect to the discovered BLE
        // Decent scale but DON'T rewrite the saved primary address — the user
        // explicitly chose WiFi and the fallback is meant to be temporary, so
        // the next app launch should retry WiFi first.
        const bool isFallbackConnect = !isWifi && bleManager.isWifiFallbackToBleActive();
        if (deferPersistence) {
            bleManager.scaleInfo(
                QStringLiteral("Validating manual WiFi scale at %1 — deferring persistence "
                               "until it answers as an HDS (%2)").arg(hostname, deviceId),
                QStringLiteral("main"));
        } else {
            // Always track this scale in the known-scales list (useful for the
            // multi-scale picker and per-scale state).
            settings.addKnownScale(deviceId, type, displayName);
            if (!isFallbackConnect) {
                settings.setPrimaryScale(deviceId);
                bleManager.setSavedScaleAddress(deviceId, type, displayName);
            } else {
                bleManager.scaleInfo(
                    QStringLiteral("WiFi fallback connected to %1 — saved WiFi primary %2 preserved")
                        .arg(displayName, settings.scaleAddress()),
                    QStringLiteral("main"));
            }
        }

        // Switch MachineState and TimingController to use physical scale instead of FlowScale
        machineState.setScale(physicalScale.get());
        timingController.setScale(physicalScale.get());

        // Connect scale to BLEManager for auto-scan control
        bleManager.setScaleDevice(physicalScale.get());

        // Forward scale-level error messages to BLEManager::errorOccurred, which
        // main.qml wires to the error dialog. Transient connect-failures are log-only
        // inside the drivers — BLE transport/service-discovery errors (#1285, #1292)
        // and WiFi mDNS-miss / host-not-found / 503 retries (#1253). What reaches
        // here is an ACTIONABLE error worth showing unconditionally — e.g. WiFi 503
        // "Another client is connected to the scale" that the retry loop can't
        // resolve, or a measurement-side condition from a refractometer ("No liquid
        // detected", "Beyond range").
        QObject::connect(physicalScale.get(), &ScaleDevice::errorOccurred,
                         &bleManager, &BLEManager::errorOccurred);

        // Disconnect FlowScale from graph and weight processor
        QObject::disconnect(&flowScale, &ScaleDevice::weightChanged,
                            &mainController, &MainController::onScaleWeightChanged);
        QObject::disconnect(&flowScale, &ScaleDevice::weightSampleReceived,
                            &weightProcessor, &WeightProcessor::processWeight);

        // Connect physical scale weight updates to MainController (permanent for scale lifetime).
        // WeightProcessor connection is managed by the connectedChanged lambda below
        // to avoid double-connecting (once here + once on connect event).
        QObject::connect(physicalScale.get(), &ScaleDevice::weightChanged,
                         &mainController, &MainController::onScaleWeightChanged);

        // Connection-priority backoff (#1093/#1176): feed the scale-agnostic
        // transport its two detection inputs. de1Device re-emits de1LinkFault
        // (stable across DE1 transport swaps; same thread as the transport).
        // WeightProcessor lives on the weight worker thread, so its
        // scaleFeedStalled → transport slot is cross-thread: pin it
        // Qt::QueuedConnection explicitly (it must run on the transport's main
        // thread, where it touches the QLowEnergyController) rather than rely
        // on AutoConnection resolving correctly — a future moveToThread reorder
        // must not silently turn this into a direct cross-thread call. Both
        // connections use the transport as context, so they auto-disconnect
        // when it is destroyed on a scale-type change. No-op for transports
        // that keep the base virtual no-ops (e.g. CoreBluetooth / iOS-macOS).
        //
        // de1LinkFault is intentionally left AutoConnection (NOT pinned): it
        // is same-thread today (DirectConnection), and if the DE1 layer is
        // ever moved to a worker thread, AutoConnection self-corrects to
        // Queued — pinning DirectConnection here would instead make that a
        // silent unsafe cross-thread call. The asymmetry with the pinned
        // scaleFeedStalled below is deliberate (that one is genuinely
        // cross-thread and must be Queued).
        if (ScaleBleTransport* scaleTransport = physicalScale->bleTransport()) {
            QObject::connect(&de1Device, &DE1Device::de1LinkFault,
                             scaleTransport, &ScaleBleTransport::onDe1LinkFault);
            QObject::connect(&weightProcessor, &WeightProcessor::scaleFeedStalled,
                             scaleTransport, &ScaleBleTransport::onScaleFeedStalled,
                             Qt::QueuedConnection);
            // Recovery counterpart (observe-mode change). Same cross-thread
            // pinning rationale as scaleFeedStalled above (WeightProcessor is
            // on the weight worker thread; the slot touches the transport's
            // main-thread state) — must be Queued, not AutoConnection.
            QObject::connect(&weightProcessor, &WeightProcessor::scaleFeedResumed,
                             scaleTransport, &ScaleBleTransport::onScaleFeedResumed,
                             Qt::QueuedConnection);
            // Confirmed-stall trigger (epoch-scope-and-stall-confirm). This —
            // not scaleFeedStalled — is what drives the enforce backoff now.
            // Same cross-thread Queued pinning rationale as above.
            QObject::connect(&weightProcessor, &WeightProcessor::scaleFeedStallConfirmed,
                             scaleTransport, &ScaleBleTransport::onScaleFeedStallConfirmed,
                             Qt::QueuedConnection);
            // #1176: tell the transport when an espresso cycle is in progress
            // (EspressoPreheating → shot end) so a triggered backoff DEFERS
            // the skip-HIGH teardown instead of bouncing the scale mid-shot;
            // an idle backoff still reconnects immediately. MachineState and
            // the transport are both main-thread → AutoConnection (same
            // rationale as the de1LinkFault wiring above). scaleTransport is
            // the context object so these auto-disconnect on a scale-type
            // change. No-op for transports keeping the base virtual no-op.
            QObject::connect(&machineState, &MachineState::espressoCycleStarted,
                             scaleTransport, [scaleTransport]() {
                                 scaleTransport->setShotActive(true);
                             });
            QObject::connect(&machineState, &MachineState::shotEnded,
                             scaleTransport, [scaleTransport]() {
                                 scaleTransport->setShotActive(false);
                             });
        }

        // When physical scale connects/disconnects, switch between physical and FlowScale
        QObject::connect(physicalScale.get(), &ScaleDevice::connectedChanged, handlerScopePtr,
                         [&physicalScale, &flowScale, &machineState, &bleManager, &mainController, &timingController, &weightProcessor, &scaleProxy, &scaleReconnectTimer, &scaleReconnectAttempt, &settings, &scaleAutoReconnectSuppressed, &scaleLcdRestorePending]() {
            if (physicalScale && physicalScale->isConnected()) {
                // Scale connected - stop any pending reconnect attempts
                scaleReconnectTimer.stop();
                scaleReconnectAttempt = 0;
                // A fresh successful connect clears any deliberate-disconnect
                // suppression (e.g. scale reconnected during DE1 sleep via a
                // manual scan).
                scaleAutoReconnectSuppressed = false;
                // BT keepScaleOn=true edge case: DE1 went to sleep (disableLcd
                // turned off the LCD), the BLE link then dropped mid-sleep, and
                // DE1 has since woken — the phaseChanged wake handler's
                // fallthrough left scaleLcdRestorePending set because the scale
                // wasn't connected at the moment of the first non-Sleep phase.
                // Restore the LCD now. WiFi never sets this flag (its onConnected
                // sends "display on" on the reconnect handshake instead).
                if (scaleLcdRestorePending) {
                    qDebug() << "Scale reconnected with LCD-restore pending - waking";
                    physicalScale->wake();
                    scaleLcdRestorePending = false;
                }
                // Scale connected - use physical scale
                machineState.setScale(physicalScale.get());
                timingController.setScale(physicalScale.get());
                scaleProxy.setTarget(physicalScale.get());
                // Disconnect FlowScale from graph and weight processor
                QObject::disconnect(&flowScale, &ScaleDevice::weightChanged,
                                    &mainController, &MainController::onScaleWeightChanged);
                QObject::disconnect(&flowScale, &ScaleDevice::weightSampleReceived,
                                    &weightProcessor, &WeightProcessor::processWeight);
                // Connect physical scale to weight processor
                QObject::connect(physicalScale.get(), &ScaleDevice::weightSampleReceived,
                                 &weightProcessor, &WeightProcessor::processWeight);
                // Notify MQTT
                if (mainController.mqttClient()) {
                    mainController.mqttClient()->onScaleConnectedChanged(true);
                }
                settings.setUseFlowScale(false);
                qDebug() << "Scale connected - switched to physical scale, disabled FlowScale";
            } else if (physicalScale) {
                // Scale disconnected - fall back to FlowScale
                machineState.setScale(&flowScale);
                timingController.setScale(&flowScale);
                scaleProxy.setTarget(&flowScale);
                // Disconnect physical scale from weight processor
                QObject::disconnect(physicalScale.get(), &ScaleDevice::weightSampleReceived,
                                    &weightProcessor, &WeightProcessor::processWeight);
                // Reconnect FlowScale to graph and weight processor
                QObject::connect(&flowScale, &ScaleDevice::weightChanged,
                                 &mainController, &MainController::onScaleWeightChanged);
                QObject::connect(&flowScale, &ScaleDevice::weightSampleReceived,
                                 &weightProcessor, &WeightProcessor::processWeight);
                // Notify MQTT
                if (mainController.mqttClient()) {
                    mainController.mqttClient()->onScaleConnectedChanged(false);
                }
                emit bleManager.scaleDisconnected();
                qDebug() << "Scale disconnected - switched to FlowScale";
                // Start auto-reconnect if we have a saved scale address, unless
                // the disconnect was a deliberate one from a DE1-sleep path —
                // either keepScaleOn=false on any transport, or keepScaleOn=true
                // on WiFi (which gracefully closes the WS so the radio can park,
                // see main.cpp's DE1-sleep handler below). In either case the
                // DE1-wake handler re-arms the reconnect.
                if (scaleAutoReconnectSuppressed) {
                    qDebug() << "Scale disconnect was deliberate (DE1-sleep) - auto-reconnect suppressed until DE1 wakes";
                } else {
                    bleManager.requestScaleReconnectRampRestart(
                        QStringLiteral("Scale disconnected"));
                }
            }
        });

        // Point the QML-facing proxy at the scale that was just created
        scaleProxy.setTarget(physicalScale.get());

        // Connect to the scale. WiFi takes a hostname; BLE takes the device info.
        if (isWifi) {
            if (auto* wifi = qobject_cast<DecentScaleWifi*>(physicalScale.get())) {
                // Wire the mDNS-resilience cache to Settings so a successful
                // hostname connect persists the peer IP for next time.
                wireWifiScaleDriver(wifi, settings, bleManager);
                // For manual entries: commit the deferred persistence ONLY
                // after the WS endpoint validates as HDS, and surface a
                // user-visible failure if validation fails. Both connections
                // are SingleShotConnection because the driver guarantees
                // recognizedAsHds and recognitionFailed are mutually exclusive
                // for a given attempt (recognitionTimer is stopped by
                // onRecognizedAsHds, and the cached-IP fallback branch of
                // onRecognitionTimeout doesn't emit recognitionFailed — only
                // the terminal give-up branch does).
                //
                // Why this exists at all: the outer 20 s scale-connection timer
                // in BLEManager is stopped at WS-connect time (it watches for
                // "ever connected", not "recognized"), so when a manual entry's
                // WS handshake succeeds but the endpoint sends no HDS frame in
                // 5 s (a non-HDS WS server, a captive portal, a future bug),
                // the manualWifiValidationFailed path through
                // onScaleConnectionTimeout would never fire. Without the
                // explicit recognitionFailed wiring below, the user would see
                // the scale appear connected for ~5 s, then disappear, with no
                // error and no opportunity to retry. (#1281 follow-up.)
                if (deferPersistence) {
                    QObject::connect(wifi, &DecentScaleWifi::recognizedAsHds,
                                     &bleManager,
                                     [&settings, &bleManager, deviceId, type, displayName, hostname]() {
                        bleManager.scaleInfo(
                            QStringLiteral("Manual WiFi scale at %1 validated as HDS — "
                                           "committing persistence (%2)").arg(hostname, deviceId),
                            QStringLiteral("main"));
                        settings.addKnownScale(deviceId, type, displayName);
                        settings.setPrimaryScale(deviceId);
                        bleManager.setSavedScaleAddress(deviceId, type, displayName);
                        emit bleManager.manualWifiValidationSucceeded(hostname);
                    },
                    Qt::SingleShotConnection);
                    QObject::connect(wifi, &DecentScaleWifi::recognitionFailed,
                                     &bleManager,
                                     [&bleManager, hostname]() {
                        bleManager.scaleWarn(
                            QStringLiteral("Manual WiFi scale at %1 connected but did not "
                                           "respond as HDS").arg(hostname),
                            QStringLiteral("main"));
                        emit bleManager.manualWifiValidationFailed(hostname);
                        // The driver's onRecognitionTimeout aborts the WS
                        // socket, but the DecentScaleWifi object itself stays
                        // alive as `physicalScale` until something resets the
                        // unique_ptr. Without this emit, that zombie disconnected
                        // driver would survive the failed validation — and the
                        // next reconnect-timer / scaleDiscovered tick would
                        // re-route into the type-unchanged branch and re-dial
                        // the unvalidated hostname against the same dead object.
                        // disconnectScaleRequested's handler clears
                        // BLEManager's reference and resets physicalScale, so
                        // the next manual attempt starts from a clean slate.
                        emit bleManager.disconnectScaleRequested();
                    },
                    Qt::SingleShotConnection);
                }
                // pendingWifiResolvedIp() carries a fresh mDNS resolution for a
                // scan selection, the "Add WiFi Scale" dialog's "Use" button, or
                // a saved-primary auto-match; it's dialed first as preferredIp
                // (never cached until verified). Empty for manual-typed entries
                // and cache-driven reconnects — those fall through to the cached
                // IP / hostname-resolve path (see BLEManager call sites).
                wifi->setEndpoint(bleManager.pendingWifiPort(), bleManager.pendingWifiPath());
                wifi->connectToHost(hostname, bleManager.pendingWifiResolvedIp());
            }
        } else {
            physicalScale->connectToDevice(device);
        }
    });

    // Handle disconnect request when starting a new scan
    QObject::connect(&bleManager, &BLEManager::disconnectScaleRequested, handlerScope.get(),
                     [&physicalScale, &flowScale, &machineState, &scaleProxy, &mainController, &bleManager, &timingController, &weightProcessor, &scaleReconnectTimer, &scaleReconnectAttempt, &scaleAutoReconnectSuppressed, &wasInSleep, &scaleLcdRestorePending]() {
        // Stop any pending auto-reconnect (user is deliberately scanning for a different scale)
        scaleReconnectTimer.stop();
        // User is selecting a new scale — clear any sleep-related state for
        // the outgoing scale so the new scale's normal reconnect/LCD behaviour
        // applies. Without this, a scan-during-sleep cycle would leave the
        // wake handler armed for the replaced scale's address and the LCD-
        // restore flag pointed at a (now-deleted) physicalScale instance.
        scaleAutoReconnectSuppressed = false;
        wasInSleep = false;
        scaleLcdRestorePending = false;
        scaleReconnectAttempt = 0;
        if (physicalScale) {
            qDebug() << "Disconnecting scale before scan";
            // Switch to FlowScale first
            machineState.setScale(&flowScale);
            timingController.setScale(&flowScale);
            scaleProxy.setTarget(&flowScale);
            // Reconnect FlowScale to graph and weight processor (physical scale is being destroyed).
            // Disconnect first to avoid duplicate connections if connectedChanged fires during reset().
            QObject::disconnect(&flowScale, &ScaleDevice::weightChanged,
                                &mainController, &MainController::onScaleWeightChanged);
            QObject::disconnect(&flowScale, &ScaleDevice::weightSampleReceived,
                                &weightProcessor, &WeightProcessor::processWeight);
            QObject::connect(&flowScale, &ScaleDevice::weightChanged,
                             &mainController, &MainController::onScaleWeightChanged);
            QObject::connect(&flowScale, &ScaleDevice::weightSampleReceived,
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

    // === Refractometer (DiFluid R1 / R2) ===
    // The `refractometer` unique_ptr itself is declared up with the engine —
    // see the note there for why the order matters. The QML-facing proxy starts with no
    // target, which is the normal state: a refractometer is only connected while the
    // post-shot review page has it open.
    RefractometerForeign::s_singletonInstance = &refractometerProxy;

    // Restore saved refractometer address for auto-reconnect
    if (!settings.savedRefractometerAddress().isEmpty()) {
        bleManager.setSavedRefractometerAddress(settings.savedRefractometerAddress(),
                                                 settings.savedRefractometerName());
    }

    QObject::connect(&bleManager, &BLEManager::refractometerDiscovered, handlerScope.get(),
                     [&refractometer, &refractometerProxy, &bleManager, &settings](const QBluetoothDeviceInfo& device) {
        bleManager.refractometerDebug(
            QStringLiteral("Discovered %1 (existing instance=%2, connected=%3)")
                .arg(getDeviceIdentifier(device),
                     refractometer ? QString::number(reinterpret_cast<quintptr>(refractometer.get()), 16)
                                    : QStringLiteral("none"),
                     (refractometer && refractometer->isConnected()) ? QStringLiteral("true")
                                                                     : QStringLiteral("false")),
            QStringLiteral("main"));
        if (refractometer && refractometer->isConnected()) {
            if (getDeviceIdentifier(device) == settings.savedRefractometerAddress()) {
                bleManager.refractometerDebug(
                    QStringLiteral("Same device already connected — ignoring discovery (no churn)"),
                    QStringLiteral("main"));
                return;  // Same device already connected — nothing to do
            }
            // Different device selected — continue to cleanup + create
        }

        // Clean up old refractometer before replacing — disconnect first (emits
        // signals while pointers are still valid), then clear raw pointer holders
        if (refractometer) {
            bleManager.refractometerDebug(
                QStringLiteral("Tearing down previous instance=%1 (connected=%2) to recreate")
                    .arg(QString::number(reinterpret_cast<quintptr>(refractometer.get()), 16),
                         refractometer->isConnected() ? QStringLiteral("true") : QStringLiteral("false")),
                QStringLiteral("main"));
            refractometer->disconnectFromDevice();
            bleManager.setRefractometerDevice(nullptr);
            refractometerProxy.setTarget(nullptr);
        }

        // Create transport using the same platform selection as scales
#if defined(Q_OS_IOS) || defined(Q_OS_MACOS)
        auto* transport = new CoreBluetoothScaleBleTransport();
#else
        auto* transport = new QtScaleBleTransport();
#endif
        // Pick the driver by advertised name. R1 prefix is checked first because
        // it's the strict prefix match; R2 is the broader heuristic.
        if (DiFluidR1::isR1Device(device.name())) {
            refractometer = std::make_unique<DiFluidR1>(transport);
        } else {
            refractometer = std::make_unique<DiFluidR2>(transport);
        }
        // INFO: the connect attempt starting is the user's story — it is what
        // precedes either "Connected and ready for measurements" or silence. The
        // instance address rides along because this is the line that pairs with a
        // teardown above when churn happens.
        bleManager.refractometerInfo(
            QStringLiteral("Connecting to %1 (instance=%2)")
                .arg(device.name(),
                     QString::number(reinterpret_cast<quintptr>(refractometer.get()), 16)),
            QStringLiteral("main"));
        // The refractometer reuses the scale transport class but is not a
        // scale: a 3rd forced-HIGH BLE link contends with the DE1 + scale and
        // the platform GATT scheduler tears the weakest one (this) down. Keep
        // the scale connection-priority / feed-stall machinery off this link.
        transport->setConnectionPriorityManaged(false);
        refractometer->connectToDevice(device);

        // Tell BLEManager about the live device (for isRefractometerConnected property)
        bleManager.setRefractometerDevice(refractometer.get());

        // Expose to QML
        refractometerProxy.setTarget(refractometer.get());

        // Save address for auto-reconnect
        settings.setSavedRefractometerAddress(getDeviceIdentifier(device));
        settings.setSavedRefractometerName(device.name());
        bleManager.setSavedRefractometerAddress(getDeviceIdentifier(device), device.name());

        // No logMessage forwarder. The R1_LOG/R2_INFO/R2_WARN macros already write
        // each line to the system log carrying [Refractometer][BLE DiFluidRx], which
        // is both what the connections view reads and what a user submits. This
        // connection existed only to copy them into the private buffer.

        // Surface actionable measurement errors ("No liquid detected", "Beyond
        // range", …) to the error dialog, mirroring the physical scale's
        // errorOccurred → BLEManager::errorOccurred wiring. requestMeasurement()
        // is user-initiated (Post-Shot Review / Settings buttons), so an error
        // here is a direct response to a tap, not background-scan noise.
        QObject::connect(refractometer.get(), &RefractometerDevice::errorOccurred,
                         &bleManager, &BLEManager::errorOccurred);

        // Auto Test lives in Settings, not on the device, because the R2 is only
        // connected while the post-shot review page is open — a device-only control
        // would be unusable almost everywhere the user might want to change it. So
        // Settings holds the intent and we push it to the device on every connect.
        // The device stores it too; writing on each connect just keeps the two in
        // step when the setting was changed with the R2 away.
        if (refractometer->supportsAutoTest()) {
            RefractometerDevice* refPtr = refractometer.get();
            // Auto Test only. Making it also raise the device's test count so unattended
            // readings were averaged was tried and dropped: three runs on hardware put
            // the single-reading scatter at about 0.011% TDS, so averaging three buys
            // roughly 0.005% — less than the 0.01% step the device reports in, and so
            // not representable in the answer at all. The driver keeps setDeviceTestCount
            // as protocol coverage; nothing sets it, and the device default of 1 stands.
            const auto applyAutoTest = [refPtr, &settings]() {
                if (!refPtr->isConnected()) return;
                refPtr->setAutoTest(settings.app()->refractometerAutoTest());
            };
            QObject::connect(refPtr, &RefractometerDevice::connectedChanged, refPtr, applyAutoTest);
            // Changing the setting while the device happens to be connected applies
            // straight away rather than waiting for the next connect.
            QObject::connect(settings.app(), &SettingsApp::refractometerAutoTestChanged,
                             refPtr, applyAutoTest);
        }

        bleManager.refractometerDebug(QStringLiteral("Created and connecting to %1").arg(device.name()),
                                      QStringLiteral("main"));
    });

    // Handle Forget Refractometer — disconnect and clean up
    QObject::connect(&bleManager, &BLEManager::disconnectRefractometerRequested, handlerScope.get(),
                     [&refractometer, &refractometerProxy, &bleManager,
                      &refractometerReconnectTimer, &refractometerReconnectAttempt]() {
        if (refractometer) {
            bleManager.refractometerDebug(QStringLiteral("Forget requested, disconnecting"),
                                          QStringLiteral("main"));
            refractometer->disconnectFromDevice();
            bleManager.setRefractometerDevice(nullptr);
            refractometerProxy.setTarget(nullptr);
            refractometer.reset();
        }
        // Stop any pending/persistent reconnect — the user forgot this device.
        // Unconditional (mirrors the scale's disconnectScaleRequested).
        //
        // LAST, not first. Everything above emits refractometerConnectedChanged
        // — disconnectFromDevice() via the device's own connectedChanged, and
        // setRefractometerDevice(nullptr) directly — and each of those re-arms
        // this tick through the connectedChanged handler below. Stopping first
        // therefore stopped a timer that was immediately re-armed: the debug log
        // showed "scheduled first retry in 5000 ms" on both sides of the stop,
        // with the tick surviving Forget and only self-cancelling ~5 s later on
        // the next-tick saved-address guard. This comment used to claim the stop
        // made teardown deterministic "rather than relying on the next-tick
        // saved-address guard"; it relied on it. Stopping here makes the claim
        // true — clearSavedRefractometer() emits this request last, so nothing
        // runs after us to re-arm.
        refractometerReconnectTimer.stop();
        refractometerReconnectAttempt = 0;
    });

    QObject::connect(&refractometerReconnectTimer, &QTimer::timeout,
                     [&bleManager, &settings, &refractometerReconnectAttempt,
                      &refractometerReconnectTimer, &reconnectDelays]() {
        // Every reason this tick stops, decided in one place, reported in one
        // line. There were four `qDebug() << "Refractometer reconnect: …"` lines
        // here, none of them marked, so the reason a paired refractometer had
        // quietly stopped retrying appeared in NO search for [Refractometer] —
        // the exact question these lines exist to answer.
        //
        // Each of these stops the tick rather than rescheduling it. Nothing is
        // lost: the corresponding change (hunt reopened, BLE re-enabled, a new
        // address saved) has its own handler that re-arms the timer. Ticking on
        // regardless is what used to write a user-visible "R2 auto-reconnect
        // attempt N" every minute forever while doing nothing at all.
        QString stopReason;
        if (settings.savedRefractometerAddress().isEmpty()) {
            stopReason = QStringLiteral("no refractometer is paired");
        } else if (!bleManager.isRefractometerHunt()) {
            // The R2 is only used on the post-shot review page (the "hunt").
            // setRefractometerHunt(true) resumes it directly — an immediate scan
            // plus onScanFinished chaining — so this timer is not needed to drive
            // on-page reconnects. The scale's reconnect is a separate always-on
            // timer and is unaffected by any of this.
            stopReason = QStringLiteral("the review page is closed — will resume when it reopens");
        } else if (bleManager.isRefractometerConnected()) {
            stopReason = QStringLiteral("already connected");
        } else if (bleManager.isDisabled()) {
            stopReason = QStringLiteral("BLE is off (simulator mode) — will resume when it is re-enabled");
        }
        if (!stopReason.isEmpty()) {
            bleManager.refractometerDebug(
                QStringLiteral("Auto-reconnect tick stopping: %1").arg(stopReason),
                QStringLiteral("main"));
            return;
        }

        // The chain is alive — reschedule without spending a rung of the ramp.
        //
        // This tick is a RECOVERY path, not a second scanner: while the hunt's
        // back-to-back chain is running there is always a scan in flight, so
        // tryDirectConnectToRefractometer() would decline with "a scan is already
        // in flight" and the attempt below would be pure fiction.
        //
        // isScanningForScales() is NOT hunt-scoped, and that matters enough to say
        // out loud — blemanager.cpp warns about this exact flag ("looks like the
        // right flag and is not: the refractometer hunt sets it too"), here in the
        // mirror image. A user-initiated scan or the scale's own always-on
        // reconnect ladder sets it too, so this branch can be taken while the hunt
        // chain is NOT what is scanning. It is still correct, for a reason worth
        // writing down rather than inferring: every scan finishes through the one
        // shared onScanFinished(), whose hunt re-chain fires regardless of who
        // started it. So any scan in flight really does mean the chain will be
        // re-kicked, and the recovery this tick provides is genuinely not needed
        // yet. It was reported
        // as fact anyway — a device log showed six consecutive "Auto-reconnect
        // attempt N" lines at INFO, every one of them immediately followed by the
        // skip, and not one of them a real attempt.
        //
        // The counter is what makes this more than cosmetic. Incrementing it on a
        // no-op walked the ramp out to its 60 s tail while nothing was wrong, so a
        // chain that died AFTER that (onScanError clears the scan flag and
        // deliberately does not re-chain — BLEManager::onScanError) waited a
        // minute for the recovery this timer exists to provide, instead of 5 s.
        // The safety net degraded itself precisely while it was idle.
        //
        // Re-arms at the FIRST rung, not the current one, and that is the whole
        // point of the branch: this is now a watchdog polling for a dead chain,
        // and the hunt windows it polls are short — four in one device session,
        // the longest 120 s. A 60 s tail would let the chain die and stay dead for
        // most of a window. The ramp itself is left where it was, so the first
        // REAL attempt after a death still starts at 5 s.
        //
        // Silent on purpose. A watchdog that finds nothing wrong is not news, and
        // at a 5 s cadence a line here would be the dominant [Refractometer] entry
        // in every hunt window — the same cry-wolf shape the INFO lines it
        // replaces already had. The story stays readable without it: "Hunt active
        // — chaining another scan" with no "Auto-reconnect attempt" between says
        // the chain was healthy and the watchdog had nothing to do.
        if (bleManager.isScanningForScales()) {
            refractometerReconnectTimer.start(reconnectDelays[0]);
            return;
        }

        // One line for the attempt, where there were three: an unmarked
        // "[R2-diag] reconnect tick attempt=N — scanning", an unmarked
        // "Refractometer reconnect: attempt N", and an appendScaleLog that reached
        // the view but not the marker. Past every guard, so this really does scan
        // — an earlier version said "— will scan" before the BLE-disabled check
        // existed, and then did not.
        //
        // INFO while the ramp is walking, DEBUG on the endless 60s tail. The tail
        // never stops while the page is open with the device absent, so at a flat
        // INFO it would dominate the view forever and say the same thing each
        // time; the first few attempts carry the news. Same reasoning as
        // BLEManager's repeat-failure budget, bounded here by the ramp itself
        // rather than a counter, because the ramp already knows where "still
        // trying, nothing new" begins.
        const int attempt = refractometerReconnectAttempt + 1;
        const QString attemptMsg = QStringLiteral("Auto-reconnect attempt %1").arg(attempt);
        if (refractometerReconnectAttempt < static_cast<int>(reconnectDelays.size())) {
            bleManager.refractometerInfo(attemptMsg, QStringLiteral("main"));
        } else {
            bleManager.refractometerDebug(attemptMsg, QStringLiteral("main"));
        }
        bleManager.tryDirectConnectToRefractometer();
        refractometerReconnectAttempt++;
        // Persistent reconnect: walk the ramp, then hold on the 60s tail
        // forever. Stops when the R2 connects or the user forgets it (both
        // guarded above).
        if (refractometerReconnectAttempt < static_cast<int>(reconnectDelays.size())) {
            refractometerReconnectTimer.start(reconnectDelays[refractometerReconnectAttempt]);
        } else {
            refractometerReconnectTimer.start(reconnectDelays.back());
        }
    });

    // Arm the R2 reconnect when it drops; stop it when it connects. Without
    // this the R2 only reconnected on app startup/resume — a powered-off R2
    // stayed dead until the next app resume (and forever on desktop, which
    // never suspends). refractometerConnectedChanged also fires transiently
    // while a fresh connection is still being set up and on Forget — the
    // saved-address guard and the !isActive() guard keep those from scrambling
    // the backoff.
    QObject::connect(&bleManager, &BLEManager::refractometerConnectedChanged, handlerScope.get(),
                     [&bleManager, &settings, &refractometerReconnectTimer,
                      &refractometerReconnectAttempt, &reconnectDelays]() {
        if (bleManager.isRefractometerConnected()) {
            refractometerReconnectTimer.stop();
            refractometerReconnectAttempt = 0;
        } else if (!settings.savedRefractometerAddress().isEmpty()
                   && !refractometerReconnectTimer.isActive()) {
            refractometerReconnectAttempt = 0;
            refractometerReconnectTimer.start(reconnectDelays[0]);
            bleManager.refractometerDebug(
                QStringLiteral("Reconnect: scheduled first retry in %1 ms").arg(reconnectDelays[0]),
                QStringLiteral("main"));
        }
    });

    // Arm/stop the R2 reconnect tick to track the review-page hunt. The R2 is
    // only pursued while the hunt is active, and the tick (which now self-stops
    // off-page) is the hunt's backoff-paced recovery path: if the back-to-back
    // scan chain dies — e.g. a scan ends via onScanError, which deliberately
    // does not re-chain — this armed tick re-kicks it. Without arming on hunt
    // activation, opening the review page for an R2 that never connected this
    // session (so no disconnect transition armed the tick) would leave the hunt
    // dependent solely on the scan-finished chain, unrecoverable if it breaks
    // until the page is reopened. Stopping on deactivation keeps no stray tick
    // running off-page. The scale reconnect is a separate timer, untouched.
    QObject::connect(&bleManager, &BLEManager::refractometerHuntChanged, handlerScope.get(),
                     [&bleManager, &settings, &refractometerReconnectTimer,
                      &refractometerReconnectAttempt, &reconnectDelays](bool active) {
        if (!active) {
            refractometerReconnectTimer.stop();
            refractometerReconnectAttempt = 0;
            return;
        }
        if (!settings.savedRefractometerAddress().isEmpty()
            && !bleManager.isRefractometerConnected()
            && !refractometerReconnectTimer.isActive()) {
            refractometerReconnectAttempt = 0;
            refractometerReconnectTimer.start(reconnectDelays[0]);
            bleManager.refractometerDebug(
                QStringLiteral("Reconnect: review page opened — arming recovery tick in %1 ms")
                    .arg(reconnectDelays[0]),
                QStringLiteral("main"));
        }
    });

    // Re-arm the R2 reconnect when BLE comes back, because the tick above stops
    // (rather than reschedules) while BLE is disabled. Without this, turning
    // simulator mode off would leave a saved R2 unreachable until the next app
    // start — the timer having quietly retired the last time it fired.
    QObject::connect(&bleManager, &BLEManager::disabledChanged, handlerScope.get(),
                     [&bleManager, &settings, &refractometerReconnectTimer,
                      &refractometerReconnectAttempt, &reconnectDelays]() {
        if (bleManager.isDisabled())
            return;
        if (settings.savedRefractometerAddress().isEmpty()
            || bleManager.isRefractometerConnected()
            || refractometerReconnectTimer.isActive())
            return;
        refractometerReconnectAttempt = 0;
        refractometerReconnectTimer.start(reconnectDelays[0]);
        bleManager.refractometerDebug(
            QStringLiteral("Reconnect: BLE re-enabled, resuming retries in %1 ms")
                .arg(reconnectDelays[0]),
            QStringLiteral("main"));
    });

    // No refractometer auto-connect at startup: the R2 is only used on the
    // post-shot review page, so it is pursued when that page opens (which fires
    // refractometerHuntChanged → arms the reconnect tick above) and disconnected
    // when it closes. A startup arm here would be dead code — tryDirectConnect
    // no-ops with the hunt off, and the tick self-stops on its first fire. The
    // scale's own startup/reconnect path is separate and unaffected.

#ifndef Q_OS_IOS
    // When USB scale discovered: wire it as the active scale (same pattern as BLE scale)
    QObject::connect(&usbScaleManager, &UsbScaleManager::scaleDiscovered,
                     [&physicalScale, &flowScale, &machineState, &mainController, &scaleProxy,
                      &bleManager, &timingController, &weightProcessor, &settings](UsbDecentScale* usbScale) {
        // Don't connect if we already have a connected BLE scale
        if (physicalScale && physicalScale->isConnected()) {
            bleManager.scaleDebug(QStringLiteral("USB scale available but a BLE scale is already "
                                                 "connected — ignoring"), QStringLiteral("main"));
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
        scaleProxy.setTarget(usbScale);

        // Disconnect FlowScale from graph and weight processor
        QObject::disconnect(&flowScale, &ScaleDevice::weightChanged,
                            &mainController, &MainController::onScaleWeightChanged);
        QObject::disconnect(&flowScale, &ScaleDevice::weightSampleReceived,
                            &weightProcessor, &WeightProcessor::processWeight);

        // Connect USB scale weight updates
        QObject::connect(usbScale, &ScaleDevice::weightChanged,
                         &mainController, &MainController::onScaleWeightChanged);
        QObject::connect(usbScale, &ScaleDevice::weightSampleReceived,
                         &weightProcessor, &WeightProcessor::processWeight);

        // NOTE: deliberately NOT wiring usbScale's errorOccurred to the error
        // dialog. It used to be, mirroring the physical (BLE/WiFi) scale's
        // wiring — but UsbDecentScale emits nothing there any more (#1658). Its
        // failures are open-refused, port-lost and unplug, and
        // UsbScaleManager::connectToScale() already handles each: it checks
        // isConnected() after open(), tears the half-open scale down, and
        // re-arms discovery. The dialog only ever interrupted that recovery,
        // with a raw "[USB Scale] …" log string as its text. A future USB error
        // that IS user-actionable should re-add this connect deliberately,
        // alongside a translated message — not inherit a pipe from a driver
        // that no longer speaks into it.

        // Register in the known-scales registry + set as primary, using the
        // stable USB identifier "usb:decent". addKnownScale + setPrimaryScale
        // write the correct scale type ("decent-usb") and display name into
        // Settings, so the Settings panel shows it and it auto-reconnects on a
        // future startup when it's still the saved primary (see the
        // usbScaleAvailable handler below). Note this ALWAYS sets the USB scale
        // as primary — unlike the BLE/WiFi scaleDiscovered handler, which gates
        // the primary write on the WiFi→BLE fallback flag. There is no USB
        // fallback path, so selecting USB is always an explicit primary choice.
        const QString kUsbScaleAddress = QStringLiteral("usb:decent");
        const QString kUsbScaleType = QStringLiteral("decent-usb");
        const QString kUsbScaleName = QStringLiteral("Half Decent Scale (USB)");
        settings.addKnownScale(kUsbScaleAddress, kUsbScaleType, kUsbScaleName);
        settings.setPrimaryScale(kUsbScaleAddress);
        bleManager.setSavedScaleAddress(kUsbScaleAddress, kUsbScaleType, kUsbScaleName);

        // Notify MQTT
        if (mainController.mqttClient()) {
            mainController.mqttClient()->onScaleConnectedChanged(true);
        }

        bleManager.scaleInfo(QStringLiteral("Switched to USB scale: %1").arg(usbScale->name()),
                             QStringLiteral("main"));
    });

    // When USB scale lost: fall back to FlowScale (or BLE scale if available)
    QObject::connect(&usbScaleManager, &UsbScaleManager::scaleLost,
                     [&physicalScale, &flowScale, &machineState, &mainController, &scaleProxy,
                      &timingController, &weightProcessor, &usbScaleManager, &bleManager]() {
        // Disconnect the USB scale's weight signals
        if (usbScaleManager.scale()) {
            QObject::disconnect(usbScaleManager.scale(), &ScaleDevice::weightChanged,
                                &mainController, &MainController::onScaleWeightChanged);
            QObject::disconnect(usbScaleManager.scale(), &ScaleDevice::weightSampleReceived,
                                &weightProcessor, &WeightProcessor::processWeight);
        }

        // Fall back to BLE scale if connected, otherwise FlowScale
        if (physicalScale && physicalScale->isConnected()) {
            machineState.setScale(physicalScale.get());
            timingController.setScale(physicalScale.get());
            scaleProxy.setTarget(physicalScale.get());
            bleManager.scaleInfo(QStringLiteral("USB scale lost — falling back to BLE scale"),
                                 QStringLiteral("main"));
        } else {
            machineState.setScale(&flowScale);
            timingController.setScale(&flowScale);
            scaleProxy.setTarget(&flowScale);
            // Reconnect FlowScale
            QObject::connect(&flowScale, &ScaleDevice::weightChanged,
                             &mainController, &MainController::onScaleWeightChanged);
            QObject::connect(&flowScale, &ScaleDevice::weightSampleReceived,
                             &weightProcessor, &WeightProcessor::processWeight);
            bleManager.scaleInfo(QStringLiteral("USB scale lost — falling back to FlowScale"),
                                 QStringLiteral("main"));
            // Surface a "scale disconnected" UI notice — same as the BLE/WiFi
            // disconnect path (see the connectedChanged handler that emits this
            // when a physical scale drops to FlowScale). Only on the FlowScale
            // fallback: falling back to a still-connected BLE scale is a switch,
            // not a disconnect, so it shouldn't flash a "disconnected" notice.
            emit bleManager.scaleDisconnected();
        }

        // Notify MQTT
        if (mainController.mqttClient()) {
            mainController.mqttClient()->onScaleConnectedChanged(false);
        }
    });

    // USB scale presence (probe-confirmed, NOT connected): list it as a
    // selectable entry, exactly like a discovered BLE/WiFi scale. Auto-connect
    // ONLY when the USB scale is the saved primary — otherwise just list it so
    // the same scale can be tested over Bluetooth/WiFi.
    QObject::connect(&usbScaleManager, &UsbScaleManager::usbScaleAvailable,
                     [&bleManager, &usbScaleManager, &settings]() {
        bleManager.setUsbScaleAvailable(true, QStringLiteral("Half Decent Scale (USB)"));
        if (settings.scaleAddress() == QStringLiteral("usb:decent")) {
            bleManager.scaleInfo(QStringLiteral("USB scale available and is saved primary — "
                                                "auto-connecting"), QStringLiteral("main"));
            usbScaleManager.connectToScale();
        } else {
            bleManager.scaleDebug(QStringLiteral("USB scale available — listed as selectable "
                                                 "(not auto-connecting)"), QStringLiteral("main"));
        }
    });
    QObject::connect(&usbScaleManager, &UsbScaleManager::usbScaleUnavailable,
                     [&bleManager]() {
        bleManager.setUsbScaleAvailable(false, QStringLiteral("Half Decent Scale (USB)"));
    });

    // User selected the USB entry in the discovered list: connect it.
    QObject::connect(&bleManager, &BLEManager::usbConnectRequested,
                     [&usbScaleManager]() {
        usbScaleManager.connectToScale();
    });

    // "Scan for Devices" covers USB too, not just BLE and WiFi. BLEManager asks
    // (it doesn't own UsbScaleManager) and the completion feeds back so the
    // composite "Scanning..." indicator waits for all three transports.
    QObject::connect(&bleManager, &BLEManager::usbProbeRequested,
                     [&usbScaleManager]() {
        usbScaleManager.probeNow();
    });
    QObject::connect(&usbScaleManager, &UsbScaleManager::probeFinished,
                     [&bleManager]() {
        bleManager.onUsbProbeFinished();
    });

    // No UsbScaleManager::logMessage forwarders. There were TWO of them — one into
    // the scale log, one into the DE1 log — because the USB scale is diagnosed from
    // both panels and neither view could read the other's channel. That was the
    // whole problem: a line had to be copied per view, and 73 hand-rolled prefixes
    // with 21 drifted qDebug/emit pairs grew out of the same gap. Every line now
    // carries [Scale][USB Scale] once, in the system log, which both views read.
#endif // !Q_OS_IOS

    // Load saved scale address for direct wake connection. Read from the
    // multi-scale-era key (primaryScaleAddress) first, then fall back to the
    // legacy single-scale key (scale/address). The Settings orphan-heal in
    // the Settings constructor already syncs them before we reach this code,
    // but reading the canonical key directly avoids depending on that sync —
    // and the legacy key was the historical source of the
    // "tryDirectConnectToScale - no saved scale address/type" bug (QML
    // checked the new key, this load used the old one, and a drift between
    // them stranded the user with no auto-connect). If primary is set, look
    // up its full entry from knownScales so we feed BLEManager the correct
    // type/name without depending on the legacy scale/type and scale/name
    // keys being in sync.
    QString savedScaleAddr = settings.primaryScaleAddress();
    QString savedScaleType;
    QString savedScaleName;
    if (!savedScaleAddr.isEmpty()) {
        for (const QVariant& v : settings.knownScales()) {
            const QVariantMap s = v.toMap();
            if (s.value("address").toString().compare(savedScaleAddr, Qt::CaseInsensitive) == 0) {
                savedScaleType = s.value("type").toString();
                savedScaleName = s.value("name").toString();
                break;
            }
        }
    }
    // Fall back to the legacy keys if primary wasn't set (older builds, or
    // a fresh install before any scale connect has written the multi-scale
    // store). The orphan-heal will reconcile this on the next launch.
    if (savedScaleAddr.isEmpty()) {
        savedScaleAddr = settings.scaleAddress();
        savedScaleType = settings.scaleType();
        savedScaleName = settings.scaleName();
    }
    if (!savedScaleAddr.isEmpty() && !savedScaleType.isEmpty()) {
        bleManager.setSavedScaleAddress(savedScaleAddr, savedScaleType, savedScaleName);
    }

    // Load saved DE1 address for direct wake connection
    QString savedDE1Addr = settings.machineAddress();
    if (!savedDE1Addr.isEmpty()) {
        bleManager.setSavedDE1Address(savedDE1Addr, QString());
    }

    // BLE scanning is now started from QML after first-run dialog is dismissed
    // This allows the user to turn on their scale before we start scanning

    // FlowScale weight connection is handled by the fallback timer and scale disconnect logic
    // Don't connect here - only one scale should feed the graph at a time


    // Expose C++ objects to QML. No `QQmlContext* context` here any more: with ScaleDevice
    // migrated there is nothing left on this path that publishes by name into the root context.
    // Also a compile-time singleton, registered via QML_FOREIGN in settings_qml.h rather than
    // macros on the class — settings.h is included by CLI tools that do not link Qt::Qml, and by
    // most of the app, so it deliberately stays free of QtQml. Same publish-the-instance shape:
    // main owns `settings` and hands it out long before QML exists.
    SettingsForeign::s_singletonInstance = &settings;
    // A compile-time-registered QML singleton (TranslationManagerForeign in
    // contextsingletons_qml.h), NOT a context property. The engine does not construct it — it is
    // the stack object above, already wired into BLE, MCP, AI, backup and accessibility — so
    // main publishes the instance and the wrapper's create() hands it back.
    //
    // Registering at compile time is what lets qmllint resolve the 3,668 QML references to this
    // name; a runtime qmlRegisterSingletonInstance() would not, because qmltyperegistrar never
    // sees it. That is the whole point of the migration, not a side effect of it.
    TranslationManagerForeign::s_singletonInstance = &translationManager;
    // MUST be called explicitly, and this is not optional bookkeeping — without it the
    // declarative registration above never runs and every translated string in the app is
    // `undefined`. Qt registers a module's compile-time types lazily, on first import, behind
    // this guard (qqmltypeloader.cpp:783, and identically qqmlimport.cpp:920):
    //
    //     auto module = QQmlMetaType::typeModule(qmldir.typeNamespace(), import->version);
    //     if (!module)
    //         QQmlMetaType::qmlRegisterModuleTypes(qmldir.typeNamespace());
    //     // else: If the module already exists, the types must have been already registered
    //
    // That last assumption is false for a module like this one, which mixes declarative
    // (QML_ELEMENT) and runtime registrations. The qmlRegisterUncreatableType<...>("Decenza",
    // ...) calls below create a type module for the URI before QML ever imports it, so at
    // import time typeModule() returns non-null, the guard short-circuits, and the generated
    // registration function is never invoked. Calling it here is the idiom Qt uses for the same
    // situation in its own qtdeclarative/tools/qml/main.cpp.
    //
    // Calling it twice would be harmless anyway: the lazy path is itself guarded on the module
    // not existing yet.
    qml_register_types_Decenza();
    // Required before QML loads: TranslationManager.translate is a QJSValue property holding a
    // callable, and it needs an engine to build that callable from. qmlEngine(this) is null for
    // an object the engine did not create, so the engine cannot be discovered from inside.
    // Without this every translated string in the app evaluates to undefined. create() repeats
    // this for engines that reach the singleton by another route. See translationmanager.h.
    translationManager.setJsEngine(&engine);
    // EmojiAssets, MarkdownRenderer and TemperatureDisplay used to be context properties over
    // objects declared here. They are QML_SINGLETONs now, engine-constructed and engine-owned,
    // so there is nothing left for main() to declare or publish. What they are for is on the
    // classes: emojiassets.h, markdownrenderer.h, temperaturedisplay.h.
    BLEManagerForeign::s_singletonInstance = &bleManager;
    // DE1Device is a QML_FOREIGN + QML_SINGLETON (contextsingletons_qml.h), not a context
    // property. Published here rather than at the declaration because the ordering that matters
    // is "before engine.load()", and this is where that is obvious.
    DE1DeviceForeign::s_singletonInstance = &de1Device;
    ScaleDeviceForeign::s_singletonInstance = &scaleProxy;
    scaleProxy.setTarget(&flowScale);  // FlowScale initially, re-pointed as hardware comes and goes
    // No "FlowScale" property. It was published "always available for diagnostics" and no QML
    // ever read it — the only occurrences of the name in qml/ are three comments in main.qml
    // about the FlowScale *fallback*, which is a different thing. Publishing an unread name is
    // not free: a context property is invisible to qmllint, so it cannot be told apart from a
    // typo at the call sites that never came.
    MachineStateForeign::s_singletonInstance = &machineState;
    ShotDataModelForeign::s_singletonInstance = &shotDataModel;
    SteamDataModelForeign::s_singletonInstance = &steamDataModel;
    SteamHealthTrackerForeign::s_singletonInstance = &steamHealthTracker;
    // Compile-time QML singleton (MainControllerForeign in contextsingletons_qml.h), not a
    // context property — same reason as every other publish in this block. The largest win remaining
    // after TranslationManager and Settings; measured reduction 916 unqualified warnings.
    //
    // BOTH halves are load-bearing and only one of them is visible to static tooling: the macros
    // put the TYPE in the registry, this call publishes the INSTANCE. Delete this line and the
    // build, qmllint and the whole suite stay green while every MainController.* binding in the
    // app resolves to null. tst_qmlregistration asserts this call exists, for that reason.
    MainControllerForeign::s_singletonInstance = &mainController;
    ProfileManagerForeign::s_singletonInstance = mainController.profileManager();
    // ScreensaverManager: QML's name for ScreensaverVideoManager. See contextsingletons_qml.h.
    ScreensaverManagerForeign::s_singletonInstance = &screensaverManager;
    AutoWakeManagerForeign::s_singletonInstance = &autoWakeManager;
    SensorCalibrationControllerForeign::s_singletonInstance = &sensorCalibration;
    BatteryManagerForeign::s_singletonInstance = &batteryManager;
    MemoryMonitorForeign::s_singletonInstance = &memoryMonitor;
    memoryMonitor.setEngine(&engine);
    AccessibilityManagerForeign::s_singletonInstance = &accessibilityManager;
    ProfileStorageForeign::s_singletonInstance = &profileStorage;
    WeatherManagerForeign::s_singletonInstance = &weatherManager;
    CrashReporterForeign::s_singletonInstance = &crashReporter;
    WidgetLibraryForeign::s_singletonInstance = &widgetLibrary;
    McpServerForeign::s_singletonInstance = &mcpServer;
    RemoteMcpAccessForeign::s_singletonInstance = &remoteMcpAccess;
    LibrarySharingForeign::s_singletonInstance = &librarySharing;
    ShotHistoryExporterForeign::s_singletonInstance = &shotHistoryExporter;
#ifndef Q_OS_IOS
    // The objects, the Foreign structs and the types themselves are all absent on iOS — that
    // platform builds no part of src/usb/ and links no SerialPort module. So the QML names do not
    // resolve there and evaluating one is a ReferenceError; every call site is short-circuited on
    // Qt.platform.os before the read, or unreachable behind one. See the note above
    // USBManagerForeign in contextsingletons_qml.h.
    USBManagerForeign::s_singletonInstance = &usbManager;
    UsbScaleManagerForeign::s_singletonInstance = &usbScaleManager;
#endif

    // Declared above `engine` (see there); only the wiring is here, where its dependencies exist.
    flowCalibrationModel.setStorage(mainController.shotHistory());
    flowCalibrationModel.setSettings(settings.calibration());
    flowCalibrationModel.setDevice(&de1Device);
    FlowCalibrationModelForeign::s_singletonInstance = &flowCalibrationModel;

    // No "AppVersion", "AppVersionCode", "PreviousCrashLog" or "PreviousDebugLogTail" properties.
    // All four were bare values with no object to hang off, and the first draft of this migration
    // invented an AppInfo singleton to hold them. Review found that three of the four already had
    // an owner:
    //   - AppVersion / AppVersionCode duplicated UpdateChecker::currentVersion /
    //     currentVersionCode, which read the same VERSION_STRING and versionCode(), are already
    //     CONSTANT and QML-registered, and are already reached as MainController.updateChecker in
    //     the very file that displayed them. Two sources of truth for one number is the drift this
    //     change exists to remove, so the holder was deleted rather than kept.
    //   - PreviousCrashLog / PreviousDebugLogTail moved onto CrashReporter (set above), which is
    //     where QML already goes to submit them.
    // No "IsDebugBuild" property. It was published from a #ifdef QT_DEBUG / #else pair and read
    // by no QML file. If a debug-only affordance is wanted later, add it back as a property on a
    // registered singleton so qmllint can see it — not as a context property, which is exactly
    // the shape that let this one sit unused without anything noticing.

#if (defined(Q_OS_WIN) || defined(Q_OS_MACOS)) && defined(QT_DEBUG) && defined(DECENZA_SIMULATOR)
    // Make GHCSimulator available to main window for window sync
    // Declared above `engine` (see there). Optional rather than mandatory: the declaration is
    // inside a debug-desktop `#if`, so on every other build there is no instance and QML reads
    // the name as undefined — which main.qml's truthy guard has always expected.
    GHCSimulatorForeign::s_singletonInstance = &ghcSimulator;
#endif

    // The "…Type" registrations that used to live here are all gone, and the reason they existed
    // is worth keeping, because it is two different reasons wearing one naming convention.
    //
    // MachineStateType, DE1DeviceType and SteamHealthTrackerType were genuine workarounds: a
    // context property resolves AHEAD of a type of the same name, so a class whose instance was
    // published as a context property could not also be registered under its plain name. Each
    // disappeared when its instance became a singleton, which needs no second name because QML
    // reads the enums straight off it (MachineState.Phase.X,
    // SteamHealthTracker.EstablishingAfterReset).
    //
    // CoffeeBagStorageType, EquipmentStorageType and UnifiedBeanSearchModelType were NOT. No
    // context property of those names ever existed — `git log -S 'setContextProperty("CoffeeBagStorage"'`
    // finds nothing. They simply copied the ...Type suffix from the neighbours above, and moved
    // for the unrelated reason in the next paragraph. An earlier draft of this comment lumped all
    // six together as context-property workarounds, which contradicted its own next sentence.
    //
    // DE1DeviceType was the last runtime qmlRegisterUncreatableType in that shape and is removed
    // here; nothing in qml/ or tests/ referenced it. AIConversation, CoffeeBagStorage,
    // EquipmentStorage and UnifiedBeanSearchModel went earlier, to QML_ELEMENT + QML_UNCREATABLE
    // in their own headers — a runtime registration is invisible to qmltyperegistrar, so it never
    // reaches Decenza.qmltypes and qmllint cannot resolve the type behind the properties that
    // return it. QML reaches those four through MainController properties, never by type name.

    // The CREATABLE types that used to be registered here — JsCanvasPainterItem,
    // StrangeAttractorRenderer, FastLineRenderer, DocumentFormatter and the four Pipe*Geometry types —
    // now carry QML_ELEMENT in their own headers. Same QML names, same creatable
    // contract, and for the same reason the uncreatable ones moved: a runtime qmlRegisterType<>
    // is invisible to qmltyperegistrar, so the type never reached Decenza.qmltypes and qmllint
    // reported every USE of it as "was not found. Did you add all imports and dependencies?" —
    // 19 warnings across six QML files, none of them a real missing import.
    //
    // Safe in their headers, and the reason is per-TARGET, not per-base-class. An earlier draft
    // said "every one already derives from a Quick or Quick3D type" — false: DocumentFormatter,
    // and the JsCanvasContext/JsCanvasGradient pair registered alongside them, all derive from
    // plain QObject. What actually holds is that documentformatter.cpp and jscanvas*.cpp are
    // compiled ONLY by the Decenza target, and the one of these that is compiled elsewhere,
    // fastlinerenderer.cpp, goes into decenza_shotlib, which links Qt6::Quick. Apply that test to
    // the next header, not the inheritance one.

    // Settings sub-object types are registered at COMPILE time via QML_FOREIGN in
    // settings_qml.h, not here. A runtime qmlRegisterUncreatableType<> is invisible to
    // qmltyperegistrar, so qmllint could not resolve them and reported every
    // Settings.<domain>.<prop> as a missing property — 1,079 of them. Same QML type names,
    // same uncreatable contract, now checkable by the linter.

    // ShotProjection is a Q_GADGET value type used as the parameter of
    // ShotHistoryStorage::shotReady. qmlRegisterUncreatableMetaObject registers
    // its meta-object so QML signal handlers can read its Q_PROPERTYs by name
    // (`shotData.finalWeightG`). qRegisterMetaType makes the type usable on
    // Qt::QueuedConnection signal/slot connections (the connection threads
    // serialize the QVariant<ShotProjection> across thread boundaries).
    qRegisterMetaType<ShotProjection>("ShotProjection");
    ShotProjection::registerMetaTypeConverters();
    qmlRegisterUncreatableMetaObject(ShotProjection::staticMetaObject,
        "Decenza", 1, 0, "ShotProjection",
        "ShotProjection is a value type returned by ShotHistoryStorage signals");

    checkpoint("Context properties & type registration");

    // Load main QML file (QTP0001 NEW policy uses /qt/qml/ prefix)
    const QUrl url(u"qrc:/qt/qml/Decenza/qml/main.qml"_s);

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
        &app, [url, &checkpoint](QObject *obj, const QUrl &objUrl) {
            if (!obj && url == objUrl)
                QCoreApplication::exit(-1);
            else if (obj)
                checkpoint("QML objectCreated");
        }, Qt::QueuedConnection);

    engine.load(url);
    checkpoint("engine.load(main.qml) returned");
    weatherManager.setQmlReady();  // Unblock weather fetch; guards against #718 (crash during QML incubation)

    // Give RelayClient a handle to the main window for screen capture
    if (!engine.rootObjects().isEmpty()) {
        QQuickWindow* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
        if (window) {
            relayClient.setWindow(window);
        }
    }

    // Simulator engine (desktop in every configuration, tablets in debug only —
    // see DECENZA_SIMULATOR in CMakeLists.txt) and GHC window (desktop debug only).
#ifdef DECENZA_SIMULATOR
    // NOTE: These must be declared outside the if-block so they survive through
    // app.exec(). Otherwise the if-block scope destroys them before the event
    // loop starts, and signal connections become dangling references (use-after-free).
    std::unique_ptr<DE1Simulator> de1SimulatorPtr;
    std::unique_ptr<SimulatedScale> simulatedScalePtr;
#if (defined(Q_OS_WIN) || defined(Q_OS_MACOS)) && defined(QT_DEBUG) && defined(DECENZA_SIMULATOR)
    std::unique_ptr<QQmlApplicationEngine> ghcEnginePtr;
#endif

    if (settings.app()->simulationMode()) {
        // Create the DE1 machine simulator
        de1SimulatorPtr = std::make_unique<DE1Simulator>();
        auto& de1Simulator = *de1SimulatorPtr;

        // Set simulator on DE1Device so commands are relayed to it
        de1Device.setSimulator(&de1Simulator);

        // Marked and at INFO, replacing a bare `qDebug() << "Creating DE1
        // Simulator..."`. This is the machine's counterpart to
        // "DE1 CONNECTED (BLE)" and it is the one line that says WHICH machine the
        // app is talking to. Without it the DE1 view opened empty until the user
        // happened to change a profile or start a shot, and nothing on the page
        // distinguished "simulated machine attached" from "no machine at all" —
        // the simulator is also why no real DE1 will ever appear, which is the
        // question a reader of that log is most likely to be asking.
        DE1_INFO_STDERR_TAGGED("Simulator",
            QStringLiteral("Simulated DE1 attached — no real machine will be "
                           "scanned for or connected this session"));

        // Give it the current profile from ProfileManager
        auto* pm = mainController.profileManager();
        // Guarded like the handlers further up: `de1Simulator` lives in a
        // unique_ptr declared AFTER `engine`, while `pm`/`settings` are declared
        // before it, so without a context object these outlive their own capture
        // and fire into freed stack during QML teardown.
        QObject::connect(pm, &ProfileManager::currentProfileChanged, handlerScope.get(),
                         [&de1Simulator, pm]() {
            de1Simulator.setProfile(pm->currentProfileObject());
        });
        // Set initial profile
        de1Simulator.setProfile(pm->currentProfileObject());

        // Connect dose from settings (affects puck resistance simulation)
        QObject::connect(settings.dye(), &SettingsDye::dyeBeanWeightChanged, handlerScope.get(),
                         [&de1Simulator, &settings]() {
            de1Simulator.setDose(settings.dye()->dyeBeanWeight());
        });
        // Set initial dose
        de1Simulator.setDose(settings.dye()->dyeBeanWeight());

        // Connect grind setting (finer grind = more resistance, can choke machine)
        QObject::connect(settings.dye(), &SettingsDye::dyeGrinderSettingChanged, handlerScope.get(),
                         [&de1Simulator, &settings]() {
            de1Simulator.setGrindSetting(settings.dye()->dyeGrinderSetting());
        });
        // Set initial grind
        de1Simulator.setGrindSetting(settings.dye()->dyeGrinderSetting());

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

        // Idle-state steam temperature updates (fired when the app commands a
        // new steam target via setShotSettings — including Off presets).
        QObject::connect(&de1Simulator, &DE1Simulator::idleSteamTempChanged,
                         &de1Device, &DE1Device::setSimulatedIdleSteamTemp);

        // Create SimulatedScale and connect it like a real scale
        simulatedScalePtr = std::make_unique<SimulatedScale>();
        auto& simulatedScale = *simulatedScalePtr;

        // Replace FlowScale with SimulatedScale for graph data
        QObject::disconnect(&flowScale, &ScaleDevice::weightChanged,
                            &mainController, &MainController::onScaleWeightChanged);
        QObject::connect(&simulatedScale, &ScaleDevice::weightChanged,
                         &mainController, &MainController::onScaleWeightChanged);

        // Set SimulatedScale as the active scale (matching physical scale pattern)
        machineState.setScale(&simulatedScale);
        timingController.setScale(&simulatedScale);
        scaleProxy.setTarget(&simulatedScale);

        // Register as a known scale so UI gated on Settings.knownScales (keepScaleOn
        // toggle, alerts toggle, known-devices picker) is reachable in simulation.
        // Idempotent: addKnownScale dedupes by address. Removed on non-sim startup below.
        const QString kSimulatedScaleAddress = QStringLiteral("sim:00:00:00:00:00:00");
        settings.addKnownScale(kSimulatedScaleAddress,
                               QStringLiteral("simulated"),
                               QStringLiteral("Simulated Scale"));
        // Promote to primary only if no real scale is paired, so the Known Devices
        // picker shows "Simulated Scale" instead of "No scale selected" without
        // clobbering a user's real scale pairing.
        if (settings.primaryScaleAddress().isEmpty()) {
            settings.setPrimaryScale(kSimulatedScaleAddress);
        }

        // Reconnect WeightProcessor from FlowScale to SimulatedScale for espresso SAW
        QObject::disconnect(&flowScale, &ScaleDevice::weightSampleReceived,
                            &weightProcessor, &WeightProcessor::processWeight);

        // Helper: apply current simulatedScaleEnabled state.
        // Enabled  → scale connected, simulator drives weight, WeightProcessor gets weight.
        // Disabled → scale disconnected (isConnected()=false suppresses SAV skip naturally),
        //            weight signals cut so SAW doesn't fire either.
        auto applySimulatedScaleEnabled = [&de1Simulator, &simulatedScale, &weightProcessor, &settings]() {
            if (settings.app()->simulatedScaleEnabled()) {
                simulatedScale.simulateConnection();
                QObject::connect(&de1Simulator, &DE1Simulator::scaleWeightChanged,
                                 &simulatedScale, &SimulatedScale::setSimulatedWeight,
                                 Qt::UniqueConnection);
                QObject::connect(&simulatedScale, &ScaleDevice::weightSampleReceived,
                                 &weightProcessor, &WeightProcessor::processWeight,
                                 Qt::UniqueConnection);
            } else {
                simulatedScale.simulateDisconnection();
                QObject::disconnect(&de1Simulator, &DE1Simulator::scaleWeightChanged,
                                    &simulatedScale, &SimulatedScale::setSimulatedWeight);
                QObject::disconnect(&simulatedScale, &ScaleDevice::weightSampleReceived,
                                    &weightProcessor, &WeightProcessor::processWeight);
            }
        };
        QObject::connect(settings.app(), &SettingsApp::simulatedScaleEnabledChanged,
                         &simulatedScale, [applySimulatedScaleEnabled]() {
            applySimulatedScaleEnabled();
        });
        applySimulatedScaleEnabled();

        // GHC Simulator window (desktop debug only — other platforms use the layout widget)
#if (defined(Q_OS_WIN) || defined(Q_OS_MACOS)) && defined(QT_DEBUG) && defined(DECENZA_SIMULATOR)
        // Configure GHC visual controller (created earlier for main window access)
        ghcSimulator.setDE1Device(&de1Device);
        ghcSimulator.setDE1Simulator(&de1Simulator);

        ghcEnginePtr = std::make_unique<QQmlApplicationEngine>();
        auto& ghcEngine = *ghcEnginePtr;
        // No "GHCSimulator" line: it is now a QML_SINGLETON too, and a singleton is per-type,
        // not per-engine — GHCSimulatorWindow.qml imports Decenza, so this engine resolves the
        // same instance main published. A context property of the same name would SHADOW it and
        // be invisible to qmllint, which is the shape #1661 took. The same goes for "DE1Device".
        //
        // No "DE1Simulator" property. GHCSimulatorWindow.qml is the only file this engine loads
        // and it never reads that name; nothing else in qml/ does either.
        // No Settings line here. Settings is a QML_FOREIGN + QML_SINGLETON (settings_qml.h) and
        // GHCSimulatorWindow.qml imports Decenza, so it resolves on this engine already. A
        // context property of the same name would SHADOW the singleton and be invisible to
        // qmllint, qmlcachegen and the language server — the #1661 shape. The TemperatureDisplay
        // line that sat beside this one went for the same reason.

        QObject::connect(&ghcEngine, &QQmlApplicationEngine::objectCreated, &app,
            [](QObject *obj, const QUrl &objUrl) {
                if (!obj) {
                    qWarning() << "GHC Simulator: Failed to load" << objUrl;
                } else {
                    qDebug() << "GHC Simulator: Window created successfully";
                }
            }, Qt::QueuedConnection);

        const QUrl ghcUrl(u"qrc:/qt/qml/Decenza/qml/simulator/GHCSimulatorWindow.qml"_s);
        ghcEngine.load(ghcUrl);
#endif // desktop GHC window
    }
#endif // DECENZA_SIMULATOR

    // Purge the simulated-scale entry when not running in simulation mode, so a
    // prior simulation session's placeholder doesn't leak into the real connection UI.
    if (!settings.app()->simulationMode()) {
        settings.removeKnownScale(QStringLiteral("sim:00:00:00:00:00:00"));
    }

#ifdef Q_OS_ANDROID
    // Set landscape orientation on Android (after QML is loaded)
    // SCREEN_ORIENTATION_SENSOR_LANDSCAPE = 6 (uses sensor to pick correct landscape)
    // Note: Using 0 (SCREEN_ORIENTATION_LANDSCAPE) causes upside-down display on some tablets
    // because "natural landscape" varies by device manufacturer
    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (activity.isValid()) {
        activity.callMethod<void>("setRequestedOrientation", "(I)V", 6);

        // Read SDK version on the Qt main thread before entering the Android UI lambda
        const jint sdkVersion = QNativeInterface::QAndroidApplication::sdkVersion();

        // Enable immersive mode - must run on UI thread
        QNativeInterface::QAndroidApplication::runOnAndroidMainThread([activity, sdkVersion]() {
            QJniObject window = activity.callObjectMethod("getWindow", "()Landroid/view/Window;");
            if (window.isValid()) {
                // FLAG_LAYOUT_NO_LIMITS = 0x200 - extend window into navigation bar area
                window.callMethod<void>("addFlags", "(I)V", 0x200);

                QJniObject decorView = window.callObjectMethod("getDecorView", "()Landroid/view/View;");

                if (sdkVersion >= 30) {
                    // API 30+ (Android 11): use WindowInsetsController (modern replacement
                    // for the deprecated setSystemUiVisibility) and tell Android to not
                    // offset the content area for system bars. Fixes a gap at the top of
                    // the screen on some tablets (e.g. Lenovo Tab One #582) where the
                    // deprecated API doesn't fully prevent content insets.
                    window.callMethod<void>("setDecorFitsSystemWindows", "(Z)V", false);

                    QJniObject insetsController = window.callObjectMethod(
                        "getInsetsController",
                        "()Landroid/view/WindowInsetsController;");
                    if (insetsController.isValid()) {
                        jint statusBars = QJniObject::callStaticMethod<jint>(
                            "android/view/WindowInsets$Type", "statusBars", "()I");
                        jint navBars = QJniObject::callStaticMethod<jint>(
                            "android/view/WindowInsets$Type", "navigationBars", "()I");
                        insetsController.callMethod<void>("hide", "(I)V",
                            statusBars | navBars);

                        // BEHAVIOR_SHOW_TRANSIENT_BARS_BY_GESTURE = 2
                        insetsController.callMethod<void>(
                            "setSystemBarsBehavior", "(I)V", 2);
                    }
                } else {
                    // API 28-29: use the legacy setSystemUiVisibility.
                    // Not called on API 30+ — mixing it with WindowInsetsController
                    // causes unpredictable behavior (one can override the other).
                    // IMMERSIVE_STICKY | FULLSCREEN | HIDE_NAVIGATION | LAYOUT_STABLE | LAYOUT_HIDE_NAVIGATION | LAYOUT_FULLSCREEN
                    // 0x1000 | 0x4 | 0x2 | 0x100 | 0x200 | 0x400 = 0x1706
                    if (decorView.isValid()) {
                        decorView.callMethod<void>("setSystemUiVisibility", "(I)V", 0x1706);
                    }
                }
            }
        });
    }

    // Sync launcher alias with persisted setting (APK updates reset component states)
    settings.app()->setLauncherMode(settings.app()->launcherMode());
#endif

    // Cross-platform lifecycle handling: manage BLE connections and system state
    // when app is suspended/resumed. Neither DE1 nor scale are put to sleep when
    // backgrounded — users may switch apps while the machine heats up.
    QObject::connect(&app, &QGuiApplication::applicationStateChanged, handlerScope.get(),
                     [&physicalScale, &bleManager, &settings, &batteryManager, &de1Device, &reconnectDelays, &de1ReconnectTimer, &de1ReconnectAttempt, &scaleAutoReconnectSuppressed, &refractometerReconnectTimer, &refractometerReconnectAttempt, &mainController](Qt::ApplicationState state) {
        static bool wasSuspended = false;

        // Log every state transition so the debug log captures pre-suspend
        // flickers (Inactive, Hidden) that precede an activity destroy.
        // Adds one line per transition — negligible noise.
        const char* name = "Unknown";
        switch (state) {
            case Qt::ApplicationSuspended: name = "Suspended"; break;
            case Qt::ApplicationHidden:    name = "Hidden";    break;
            case Qt::ApplicationInactive:  name = "Inactive";  break;
            case Qt::ApplicationActive:    name = "Active";    break;
        }
        // Not bracketed, for the same reason as the startup timing line above.
            qDebug().noquote() << QStringLiteral("App state changed -> %1").arg(name);

        // Gate BatteryManager's poll while suspended; re-arm on any other state
        // so a missed Active transition can't strand it (see m_appActive in
        // batterymanager.h for the full rationale).
        batteryManager.setAppActive(state != Qt::ApplicationSuspended);

        if (state == Qt::ApplicationSuspended) {
            wasSuspended = true;

#ifdef Q_OS_ANDROID
            // Disable accessibility bridge before surface is destroyed.
            // Prevents deadlock between QtAndroidAccessibility::runInObjectContext()
            // and QAndroidPlatformOpenGLWindow::eglSurface() that causes SIGABRT
            // when the render thread tries to swap buffers after Android destroys
            // the EGL surface while the accessibility thread holds the lock.
            QAccessible::setActive(false);
#endif

            // Scale is NOT put to sleep when the app is backgrounded — scale sleep
            // is tied to the machine going to sleep, not the app lifecycle. Users
            // frequently switch to other apps (e.g., Claude) and expect the scale
            // to remain connected when they return. On Android, the scale BLE
            // connection stays alive only while the DE1 foreground service is
            // running (provides the wake lock); without it the OS may freeze
            // the event loop and drop the scale. The reconnect-on-resume path
            // below handles that case.

            // DE1 intentionally NOT put to sleep - user may be checking other apps
            // while machine heats up

            // IMPORTANT: Ensure charger is ON when app goes to background
            // This prevents tablet from dying if user doesn't return to the app.
            // Previously skipped on iOS because the old BLE command queue raced with
            // CoreBluetooth suspension, causing SIGSEGV. ensureChargerOn() uses
            // setUsbChargerOnUrgent(), which puts the write at the FRONT of the shared
            // GATT queue. That is a position, not a bypass, and since the queue moved
            // to posted dispatch the write leaves one event-loop turn after this call
            // rather than during it.
            //
            // That turn is available: this handler runs FROM the event loop (it is a
            // QGuiApplication::applicationStateChanged slot), so returning from it
            // returns to the loop, which then delivers the queued dispatch. iOS does
            // not suspend us inside our own slot. The write was never synchronous in
            // the sense that mattered anyway — QLowEnergyService::writeCharacteristic
            // only ever handed the payload to DarwinBTCentralManager's own request
            // queue, which drains on the LE dispatch queue. The bluetooth-central
            // background mode keeps CoreBluetooth alive longer during backgrounding.
            batteryManager.ensureChargerOn();

            // Flush queued database writes LAST in this branch, and last for the
            // same reason the quit-path call is last: it pumps events, so it must
            // not be queued in front of anything time-critical. Here that is
            // QAccessible::setActive(false) above — which exists to close an
            // EGL-surface/accessibility deadlock window — and ensureChargerOn(),
            // whose comment records a SIGSEGV from racing CoreBluetooth teardown.
            // The database is local and loses nothing by waiting.
            //
            // Why it is needed at all: aboutToQuit is not emitted when the OS kills
            // a backgrounded process, which on Android is the common way this app
            // ends, so this is the last hook a queued dose/note/rating gets.
            //
            // Usually a no-op via drainDbWork()'s early-out — the worker is a
            // separate thread that keeps draining after this returns, so there is
            // normally nothing outstanding by the time it is asked. What it defends
            // against is the process being frozen (Android's cached-process freezer,
            // Doze) or killed before the worker reaches a queued task.
            //
            // It cannot hang the UI thread here, and that safety is NOT obvious:
            // drainDbWork relies on timers, and Android's dispatcher suppresses
            // timer activation while suspended (QEventLoop::X11ExcludeTimers,
            // qandroideventdispatcher.cpp:54-56) — which would block until resume.
            // It does not apply to us because that stopper is only registered when
            // blockEventLoopsWhenSuspended() is true (qandroideventdispatcher.cpp:12-14),
            // and android/AndroidManifest.xml.in declares
            // android.app.background_running="true", which sets
            // QT_BLOCK_EVENT_LOOPS_WHEN_SUSPENDED=0 (QtLoader.java, androidjnimain.cpp).
            // Flip that manifest line and this becomes a blocking wait.
            mainController.drainDbWork(250, MainController::DrainReason::Backgrounding);
        }
        else if (state == Qt::ApplicationActive && wasSuspended) {
            qDebug() << "App resumed from suspended state";
            wasSuspended = false;
            mainController.hdsFirmwareUpdate()->checkForUpdates();

#ifdef Q_OS_ANDROID
            // Re-enable accessibility bridge now that the EGL surface is valid again
            QAccessible::setActive(true);
#endif

            // Sync settings from disk to ensure we have latest values
            // (prevents theme colors from falling back to defaults on wake)
            settings.sync();

            // Try to reconnect/wake DE1 — reset the reconnect counter so we get
            // a fresh set of retries after resume (the DE1 may still be waking up).
            if (!de1Device.isConnected() && !de1Device.isConnecting()) {
                de1ReconnectAttempt = 0;
                if (!de1ReconnectTimer.isActive()) {
                    de1ReconnectTimer.start(500);  // Short delay to let BLE stack initialize
                }
            }

            // Scale was not put to sleep on suspend, so it should still be connected.
            // If the OS dropped the BLE connection anyway, start the reconnect sequence.
            // If the scale was deliberately disconnected on DE1 sleep, the user
            // returning to the app is a signal that they want it back — clear the
            // suppression flag so the normal reconnect runs.
            scaleAutoReconnectSuppressed = false;
            if (physicalScale && physicalScale->isConnected()) {
                qDebug() << "App resumed - scale still connected";
            } else {
                bleManager.requestScaleReconnectRampRestart(QStringLiteral("App resumed"));
            }

            // Refractometer disconnected while suspended - (re)start its
            // reconnect tick. Unlike the scale path above, this only does real
            // work while the review-page hunt is active: off that page the tick
            // fires once and self-stops (the R2 is not pursued off-page), and
            // hunt activation re-arms it. Arming here is harmless in that case
            // and covers a resume that lands directly on the review page.
            if (!bleManager.isRefractometerConnected()
                && !settings.savedRefractometerAddress().isEmpty()
                && !refractometerReconnectTimer.isActive()) {
                refractometerReconnectAttempt = 0;
                refractometerReconnectTimer.start(reconnectDelays[0]);
                qDebug() << "App resumed - arming refractometer reconnect tick (effective only while hunting)";
            }

            // Resume smart charging check now that app is active again
            batteryManager.checkBattery();
        }
    });

    // Pause BLE scan-reconnect loops while the screensaver is showing.
    //
    // With a saved-but-absent scale (or refractometer), the reconnect timers
    // keep running 60 s passive scans indefinitely. Each scan parks the radio
    // active for the Qt LowEnergy discovery timeout (currently 15 s, set in
    // BLEManager::setLowEnergyDiscoveryTimeout). Over the unattended hours the
    // user typically spends on the screensaver, those scans can run hundreds
    // of times and contend with the DE1 link's keepalive traffic. Issue #1309
    // hypothesised this state as a contributing factor to a P80X DE1 wedge:
    // ~7 h of scale-absent scans during screensaver before an MMR keepalive
    // write timed out and the link couldn't recover. Root cause isn't proven —
    // this PR cuts the most plausible upstream input.
    //
    // The screensaver doesn't suspend the app (we're still Qt::ApplicationActive),
    // so the existing applicationStateChanged path above doesn't catch it. We
    // mirror that path here, stopping both timers on entry and restarting them
    // on exit. Resume gates differ between the two: scale checks saved address,
    // not connected, not suppressed, not USB; refractometer checks saved address
    // and not connected (no suppression flag or USB-routing for it). Note the
    // refractometer restart only does real work while the review-page hunt is
    // active — off that page its tick fires once and self-stops.
    QObject::connect(&screensaverManager, &ScreensaverVideoManager::screensaverActiveChanged,
                     handlerScope.get(), [&screensaverManager, &bleManager, &settings,
                      &scaleReconnectTimer, &reconnectDelays,
                      &refractometerReconnectTimer, &refractometerReconnectAttempt]() {
        const bool active = screensaverManager.screensaverActive();
        if (active) {
            if (scaleReconnectTimer.isActive()) {
                qDebug() << "Screensaver entered - pausing scale reconnect loop";
                scaleReconnectTimer.stop();
            }
            if (refractometerReconnectTimer.isActive()) {
                qDebug() << "Screensaver entered - pausing refractometer reconnect loop";
                refractometerReconnectTimer.stop();
            }
            return;
        }

        // We deliberately do NOT clear scaleAutoReconnectSuppressed here: a brief
        // glance at the tablet isn't the same signal as switching back from
        // another app, and DE1 sleep semantics already manage that flag.
        bleManager.requestScaleReconnectRampRestart(QStringLiteral("Screensaver exited"));

        if (!bleManager.isRefractometerConnected()
            && !settings.savedRefractometerAddress().isEmpty()
            && !refractometerReconnectTimer.isActive()) {
            refractometerReconnectAttempt = 0;
            refractometerReconnectTimer.start(reconnectDelays[0]);
            qDebug() << "Screensaver exited - resuming refractometer reconnect sequence";
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

    // Manage scale power state when the DE1 sleeps/wakes.
    //   keepScaleOn=true  (default): send disableLcd(). On BT the link stays
    //                                connected and LCD re-enables via wake() on
    //                                resume. On WiFi we additionally close the
    //                                WS (after disableLcd) so the tablet's WiFi
    //                                radio can park during DE1 sleep — there's
    //                                no reason to keep a live TCP session open
    //                                to the scale while the app is idle, and
    //                                Android's WiFi power-save reliably kills
    //                                the radio anyway (HDS AsyncTCP then reaps
    //                                us at 30 s of unacked data, leaving a
    //                                stale dirty disconnect). LCD comes back
    //                                on via DecentScaleWifi::onConnected's
    //                                "display on" when the WS reconnects on
    //                                DE1 wake.
    //   keepScaleOn=false: send sleep() then drop the link once the write
    //                      completes. Matches de1app's default for battery-only
    //                      scales. Auto-reconnect is suppressed via
    //                      scaleAutoReconnectSuppressed until the DE1 wakes.
    // de1EverAwake: suppress Sleep reaction on initial connect (DE1's default
    // BLE state is Sleep, so MachineState transitions Disconnected→Sleep before
    // the real state arrives).
    // de1EverAwake + wasInSleep are declared at the top of main() (see the
    // "DE1-phase tracking flags" block) so the disconnectScaleRequested
    // handler can clear them when the user swaps to a different scale.
    QObject::connect(&machineState, &MachineState::phaseChanged, handlerScope.get(),
                     [&physicalScale, &machineState, &settings, &de1EverAwake,
                      &wasInSleep, &scaleLcdRestorePending, &bleManager,
                      &scaleAutoReconnectSuppressed, &scaleReconnectTimer]() {
        auto phase = machineState.phase();
        if (phase == MachineState::Phase::Disconnected) {
            de1EverAwake = false;
            wasInSleep = false;
        } else if (phase == MachineState::Phase::Sleep) {
            // Only treat this as a real sleep event if DE1 was previously
            // awake — otherwise it's the initial-connect-while-sleeping case
            // and we don't want the next non-Sleep phase to fire wake actions
            // for a "wake event" that never had a matching sleep.
            if (de1EverAwake) {
                wasInSleep = true;
            }
            if (de1EverAwake && physicalScale && physicalScale->isConnected()) {
                if (settings.keepScaleOn()) {
                    qDebug() << "DE1 going to sleep - disabling scale LCD (keepScaleOn=true)";
                    physicalScale->disableLcd();
                    // WiFi only: also gracefully close the WS so the tablet's
                    // WiFi radio can park and the HDS doesn't reap us mid-sleep.
                    // BT stays connected — the BLE radio doesn't have the same
                    // idle-park pathology, and BT users have years of expecting
                    // the link to survive the screensaver. See comment above
                    // and DecentScaleWifi::onConnected for the LCD-restore.
                    if (physicalScale->type() == ScaleTypeIds::scaleTypeId(ScaleType::DecentScaleWifi)) {
                        qDebug() << "DE1 sleep + WiFi scale - closing WS for the sleep interval";
                        scaleAutoReconnectSuppressed = true;
                        physicalScale->disconnectFromScale();
                    } else {
                        // BT: track that the LCD is off so connectedChanged can
                        // restore it if the BLE link drops mid-sleep and reconnects
                        // after DE1 wake — the phaseChanged wake handler can only
                        // fire wake() if the scale is connected at the moment of
                        // the first non-Sleep phase transition.
                        scaleLcdRestorePending = true;
                    }
                } else {
                    qDebug() << "DE1 going to sleep - putting scale to sleep and disconnecting (keepScaleOn=false)";
                    // Suppress the reconnect timer that connectedChanged would
                    // otherwise schedule when disconnectFromScale() fires.
                    scaleAutoReconnectSuppressed = true;
                    QObject::connect(physicalScale.get(), &ScaleDevice::sleepCompleted,
                                     physicalScale.get(),
                                     [scale = physicalScale.get()]() {
                                         if (scale) scale->disconnectFromScale();
                                     },
                                     Qt::SingleShotConnection);
                    physicalScale->sleep();
                }
            }
        } else {
            // Any non-Sleep, non-Disconnected phase (Idle / Heating / Ready /
            // EspressoPreheating / Pouring / …). Fire the wake actions exactly
            // once on the first transition out of Sleep — the destination phase
            // is typically Phase::Heating or Phase::Ready and may never reach
            // Phase::Idle in a single session, so the previous Phase::Idle-only
            // gate missed the wake event entirely.
            if (wasInSleep) {
                wasInSleep = false;
                if (physicalScale && physicalScale->isConnected()
                    && !scaleAutoReconnectSuppressed) {
                    // BT keepScaleOn=true happy path: scale stayed connected
                    // through DE1 sleep. LCD was turned off by disableLcd() —
                    // restore it. (WiFi keepScaleOn=true closes the WS and
                    // takes the next branch; its onConnected() handles LCD
                    // restore via "display on" on the reconnect handshake.)
                    qDebug() << "DE1 woke up - waking scale LCD";
                    physicalScale->wake();
                    scaleLcdRestorePending = false;
                } else if (scaleAutoReconnectSuppressed
                           && !settings.scaleAddress().isEmpty()) {
                    // We deliberately disconnected on DE1 sleep and suppressed
                    // the auto-reconnect — either via keepScaleOn=false (any
                    // transport) or via the keepScaleOn=true + WiFi graceful-
                    // close path above. Re-arm the reconnect sequence now that
                    // the DE1 is back. A fresh reconnect's onConnected() will
                    // send `display on` itself, so no wake() needed here.
                    scaleAutoReconnectSuppressed = false;
                    // Short first attempt on this path only: the WiFi scale is
                    // known alive (we closed the WS ourselves) and a powered-off
                    // BT scale fails fast into the normal backoff. reconnectDelays[0]
                    // was sized for unexpected drops where the radio might need to
                    // settle; neither applies here.
                    constexpr int kWakeReconnectFirstAttemptMs = 200;
                    bleManager.requestScaleReconnectRampRestart(
                        QStringLiteral("DE1 woke up"), kWakeReconnectFirstAttemptMs);
                } else {
                    // Neither branch fired: scale exists but is mid-reconnect
                    // (BT link dropped during sleep — connectedChanged armed a
                    // reconnect on its own) or app-resume already cleared the
                    // suppression flag. Leave scaleLcdRestorePending intact so
                    // connectedChanged restores the LCD when the scale lands.
                    qDebug() << "DE1 woke up - no immediate wake action"
                             << "(physicalScale=" << (physicalScale ? "yes" : "no")
                             << "connected=" << (physicalScale && physicalScale->isConnected())
                             << "suppressed=" << scaleAutoReconnectSuppressed
                             << "lcdRestorePending=" << scaleLcdRestorePending << ")";

                    // …but "mid-reconnect" may mean "deep in the 5-min tail",
                    // and a DE1 that just woke is a strong signal the user is
                    // back at the machine with the scale switched on. Restart
                    // the ramp from the top so the scale is picked up in 5 s
                    // rather than up to 5 min. Only when the ladder is actually
                    // running: with no saved scale, or a USB primary (owned by
                    // UsbScaleManager), there is nothing to re-arm.
                    // isActive stays a caller gate: the ladder is deliberately
                    // stopped while the screensaver runs, and a DE1 wake behind it
                    // should not resurrect it.
                    if (scaleReconnectTimer.isActive()) {
                        bleManager.requestScaleReconnectRampRestart(
                            QStringLiteral("DE1 woke up"));
                    }
                }
            }
            de1EverAwake = true;
        }
    });

    // Cleanup on exit
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&accessibilityManager, &batteryManager, &de1Device, &de1ReconnectTimer, &physicalScale, &engine, &weightThread, &relayClient, &machineStatusSnapshot, &mainController, &scaleReconnectTimer, &shotHistoryExporter]() {
        qDebug() << "Application exiting - shutting down devices";

        // Leave an honest "disconnected" snapshot so the Home Screen widget
        // doesn't keep showing the last live state after the app is gone.
        machineStatusSnapshot.publishDisconnected();

        // Stop relay client and screen capture FIRST — the capture timer grabs
        // frames from the render thread. If it fires during the BLE drain wait
        // loop below, grabWindow() can deadlock against a stopping render thread,
        // freezing the app on quit (especially on Android/Samsung A8).
        // Use shutdown() instead of setEnabled(false) because setEnabled(false) is
        // a no-op when already disabled, and uses async socket close. shutdown()
        // unconditionally destroys the capture service and aborts the socket.
        relayClient.shutdown();

        // Set QML shuttingDown flag to prevent screensaver from activating.
        // Qt.quit() does NOT trigger ApplicationWindow.onClosing, so the QML-side
        // shuttingDown flag may not be set. Setting it here covers all exit paths.
        if (!engine.rootObjects().isEmpty()) {
            engine.rootObjects().constFirst()->setProperty("shuttingDown", true);
        }

        // Stop weight processor thread first (before BLE shutdown).
        // Any pending SOW commands are no longer needed since we're exiting.
        weightThread.quit();
        weightThread.wait(1000);

        bool needBleWait = false;

        // transport() is null in simulation mode (sim uses the simulator, not a
        // real transport), yet de1Device.isConnected() is unconditionally true
        // in sim. Gate the drain wait on a real connected transport: otherwise
        // every sim-mode quit enters the wait with no queueDrained source and
        // always trips the safety-net timeout with a misleading warning.
        auto* de1Transport = de1Device.transport();
        const bool de1TransportConnected = de1Transport && de1Transport->isConnected();

        // Put DE1 to sleep if connected (this is more reliable than QML onClosing on mobile)
        if (de1Device.isConnected()) {
            qDebug() << "Sending DE1 to sleep on app exit";
            de1Device.goToSleep();
            needBleWait = de1TransportConnected;
        }

        // Put scale to sleep if connected
        if (physicalScale && physicalScale->isConnected()) {
            qDebug() << "Sending physical scale to sleep on app exit";
            needBleWait = true;
        }

        // IMPORTANT: Ensure charger is ON before exiting.
        // Matches de1app's app_exit behaviour — always leave the charger ON so
        // the tablet can charge while the app is not running to manage it.
        //
        // BEFORE the drain wait below, not after, and that ordering is the whole
        // point. The write goes to the FRONT of the shared GATT queue and its
        // dispatch is posted, so it needs one event-loop turn; after the wait
        // there may be no such turn on a quiet quit (drainDbWork() returns
        // immediately when nothing is queued, and the export loop runs zero
        // iterations), and de1Device.disconnect() below would then forget() the
        // write unissued — leaving the DE1 USB port off, which is the
        // tablet-dies-overnight case this call exists to prevent.
        batteryManager.ensureChargerOn();

        // Wait for BLE writes to complete before exiting
        if (needBleWait) {
            QEventLoop waitLoop;
            bool drained = false;
            int timeoutMs = 1500; // Safety-net timeout

            if (de1TransportConnected) {
                QObject::connect(de1Transport, &DE1Transport::queueDrained,
                                 &waitLoop, [&]() { drained = true; waitLoop.quit(); });
                QObject::connect(de1Transport, &DE1Transport::disconnected,
                                 &waitLoop, [&]() { waitLoop.quit(); });
                timeoutMs = 2000;
            }

            if (physicalScale && physicalScale->isConnected()) {
                QObject::connect(physicalScale.get(), &ScaleDevice::sleepCompleted,
                                 &waitLoop, [&]() { drained = true; waitLoop.quit(); });
                physicalScale->sleep();
            }

            qDebug() << "Waiting for BLE queue to drain before exit...";
            QTimer::singleShot(timeoutMs, &waitLoop, [&]() { waitLoop.quit(); });
            waitLoop.exec();

            if (drained)
                qDebug() << "BLE queue drained successfully, exiting.";
            else
                qWarning() << "BLE queue drain timed out after" << timeoutMs << "ms — sleep command may not have been delivered.";
        }

        // Neutralize the auto-reconnect path before BLE disconnect, otherwise
        // de1Device.disconnect() below fires connectedChanged, which triggers
        // the auto-reconnect lambda and schedules a 5s QTimer. That timer
        // stays alive through stack unwinding and hangs the event dispatcher
        // on Android when it tries to fire after teardown.
        //
        // Stop the timer first (belt) and disconnect only connectedChanged
        // (suspenders). Do NOT use the wildcard form
        // QObject::disconnect(&de1Device, nullptr, nullptr, nullptr) here:
        // de1Device is reachable from QML, so QQmlEngine holds internal
        // bookkeeping tied to its lifetime. The wildcard disconnect form
        // acquires receiver locks in a different order than the per-signal
        // pointer form, and on Android shutdown that contends with the QML
        // engine and hard-deadlocks the main thread (#877). The per-signal
        // form has no such problem.
        //
        // This said "exposed to QML via setContextProperty" until DE1Device
        // became a QML_SINGLETON (contextsingletons_qml.h). The mechanism
        // changed — the engine now tracks it through QQmlData and
        // addOwnedObject rather than a context property's destroyed()
        // connection — but the hazard and the fix are unchanged, so do not
        // read the new registration as a reason to relax this back to the
        // wildcard form.
        de1ReconnectTimer.stop();
        QObject::disconnect(&de1Device, &DE1Device::connectedChanged, nullptr, nullptr);
        scaleReconnectTimer.stop();

        // Let the database write workers finish before the storages are
        // destroyed. The BLE work above protects the machine; this protects the
        // user's data. Until now nothing waited: a dose, note, rating or
        // Visualizer id written seconds before quit could be discarded by
        // ~SerialDbWorker with only a warning nobody was around to read.
        //
        // Placed HERE, not earlier, for two reasons found in review:
        //
        //  - It pumps events, and every reconnect ladder must be disarmed first.
        //    A DE1 or scale reconnect tick landing inside the drain would start a
        //    BLE scan microseconds before the disconnect below — the stale-GATT
        //    state the comment above exists to avoid. Before this drain existed,
        //    a quit with nothing BLE-connected processed no events here at all,
        //    so that window is new and had to be closed rather than inherited.
        //  - Both BLE waits above talk to hardware over a link that is about to
        //    go away, including the charger write labelled important for safety.
        //    The database is local and loses nothing by waiting, so it must not
        //    be queued in front of either.
        //
        // ExcludeUserInputEvents because a second tap on Quit, delivered inside
        // this loop, reaches QCoreApplication::exit() — which exits EVERY loop in
        // data->eventLoops (qcoreapplication.cpp:1520-1529), including this one.
        // That would abandon the drain and discard exactly the write it is here
        // to save, at the hand of an impatient user.
        //
        // Bounded for the same reason the BLE waits are: Android kills a process
        // that lingers on quit.
        mainController.drainDbWork();

        // A drained write may have just spawned an export thread (the exporter
        // subscribes to shotSaved/shotMetadataUpdated), and this object is
        // destroyed BEFORE MainController at scope exit — every export thread
        // body starts with `if (*destroyed) return;`. Waiting on the database
        // alone therefore made the DB row current while the exported JSON stayed
        // stale, silently. Same bounded shape as the drain above.
        {
            QElapsedTimer waited;
            waited.start();
            while (!shotHistoryExporter.isExportWorkIdle() && waited.elapsed() < 750)
                QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
            if (!shotHistoryExporter.isExportWorkIdle())
                STORAGE_WARN_STDERR("Export", QStringLiteral(
                    "shot export threads still running at exit - the exported JSON for a "
                    "just-edited shot may be stale"));
        }

        // Explicitly disconnect BLE so the GATT connection is released cleanly.
        // Without this, if the app is force-killed (e.g. after a hang), Android's
        // Bluetooth stack keeps the stale GATT connection — on Samsung devices this
        // can prevent the app from reconnecting until the device is rebooted.
        de1Device.disconnect();
        if (physicalScale) {
            physicalScale->disconnectFromScale();
        }

        // Note: no need to null context properties here — but NOT because of a
        // blanket "everything is declared before the engine" guarantee. This
        // comment used to claim exactly that, and a refractometer teardown
        // crash proved it wrong: the device was declared AFTER the engine, so
        // it died first, while bindings reading it were still live.
        //
        // What actually holds: most context-property backing objects are
        // declared above `engine` and so outlive it (`refractometer` now among
        // them, and `flowCalibrationModel`, which moved up there when it became
        // a singleton — a singleton has NO self-nulling mechanism, so for it
        // declaration order is the whole of the safety, not a belt on a brace).
        // Where that is not true — `de1SimulatorPtr`, see the note after
        // app.exec(); also GHCSimulator further down, still a context property —
        // safety comes from QML dropping a context property itself when its
        // object emits destroyed(), plus the C++ side holding it via QPointer
        // so it self-nulls at the same moment (BLEManager::m_refractometerDevice
        // is the worked example). A new context-property object declared after
        // the engine and held by a raw pointer has neither, and would need one.

        // Disable Qt's accessibility bridge before window destruction
        // This prevents iOS crash (SIGBUS) where the accessibility system tries to
        // sync with already-destroyed QML items during app exit
        QAccessible::setActive(false);

        // Shutdown accessibility to stop TTS before any other cleanup
        // This prevents race conditions with Android's hwuiTask thread
        accessibilityManager.shutdown();
    });

    int result = app.exec();

    // Sever the main() signal handlers before any local they captured is
    // destroyed — see the `handlerScope` comment where it is declared. This has
    // to stay ahead of ~QQmlApplicationEngine, which runs at scope exit below and
    // can still re-enter C++ setters that emit into those lambdas.
    handlerScope.reset();

    // Same lifetime problem, but NOT a signal connection, so handlerScope cannot
    // reach it: de1SimulatorPtr is declared after `engine` and dies before
    // de1Device (declared at the top), which holds a RAW DE1Simulator*. Clear it
    // while both ends are still alive.
    //
    // The simulated scale needs no equivalent: MachineState::m_scale and
    // ShotTimingController::m_scale are both QPointer, so they self-null when the
    // scale is destroyed. Only this pointer is unguarded.
    de1Device.setSimulator(nullptr);

    // DE1 signals already disconnected in aboutToQuit handler before BLE disconnect.

    // Disable crash handler before cleanup - crashes during C++ runtime destruction
    // are not actionable and shouldn't prompt users to submit bug reports
    CrashHandler::uninstall();

    // Drain remaining log messages and restore default handler.
    // Must be after CrashHandler (reverse of installation order).
    AsyncLogger::uninstall();

    return result;
}
