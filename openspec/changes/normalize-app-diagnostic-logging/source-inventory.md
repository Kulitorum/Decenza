# Runtime emitter inventory

The table records the initial lexical scan, which ignores strings and comments. All listed runtime calls now use registered helpers. Existing helpers, mixed-owner files, headers, QML/JS and dormant platform sources are covered by the expanded gate. Counts below describe the initial migration inventory, not runtime frequency.

| File | Initial raw calls by destination |
| --- | --- |
| qml/Theme.qml | Font: 1, Theme: 1 |
| qml/components/BackgroundSurface.qml | Theme: 2 |
| qml/components/BagCard.qml | BeanBase: 1 |
| qml/components/ChangeBeansDialog.qml | BeanBase: 2 |
| qml/components/DrinkType.qml | Recipes: 2 |
| qml/components/FavoritesListView.qml | App: 1 |
| qml/components/FlipClockScreensaver.qml | Screensaver: 1 |
| qml/components/GrindPickerDialog.qml | App: 1 |
| qml/components/LastShotChartRenderer.qml | Theme: 3 |
| qml/components/LastShotChartSource.qml | Theme: 3 |
| qml/components/ShotMapScreensaver.qml | Screensaver: 4 |
| qml/components/layout/CustomEditorPopup.qml | App: 7 |
| qml/components/layout/LayoutActions.qml | App: 1 |
| qml/components/layout/LayoutItemDelegate.qml | App: 3 |
| qml/components/layout/ScreensaverEditorPopup.qml | Shot: 1 |
| qml/components/layout/ShotPlanConfig.js | Shot: 1 |
| qml/components/layout/items/CustomItem.qml | App: 1 |
| qml/components/layout/items/EspressoItem.qml | Shot: 2 |
| qml/components/layout/items/FlushItem.qml | DE1: 1 |
| qml/components/layout/items/HotWaterItem.qml | DE1: 1 |
| qml/components/layout/items/RecipesItem.qml | Recipes: 3 |
| qml/components/layout/items/SteamItem.qml | Steam: 1 |
| qml/components/library/LibraryPanel.qml | App: 1 |
| qml/components/qrcode.js | App: 1 |
| qml/main.qml | App: 10, AutoLoad: 2, AutoSleep: 8, DE1: 5, Keyboard: 10, Scale: 2, Screensaver: 3, Shot: 6, Steam: 3 |
| qml/pages/EspressoPage.qml | Shot: 1 |
| qml/pages/FlushPage.qml | DE1: 2 |
| qml/pages/IdlePage.qml | DE1: 2, Recipes: 3, Shot: 4, Steam: 1 |
| qml/pages/PostShotReviewPage.qml | Refractometer: 2, Shot: 5, Steam: 1 |
| qml/pages/ProfileImportPage.qml | Profiles: 1 |
| qml/pages/ProfileSelectorPage.qml | Profiles: 1 |
| qml/pages/RecipeWizardPage.qml | Recipes: 12 |
| qml/pages/RecipesPage.qml | Recipes: 1 |
| qml/pages/ScreensaverPage.qml | Screensaver: 14 |
| qml/pages/SettingsPage.qml | App: 5 |
| qml/pages/ShotDetailPage.qml | Shot: 2, Steam: 1 |
| qml/pages/SimpleProfileEditorPage.qml | Profiles: 1, Recipes: 1 |
| qml/pages/SteamPage.qml | Steam: 6 |
| qml/pages/settings/SettingsConnectionsTab.qml | Scale: 2 |
| qml/pages/settings/SettingsDebugTab.qml | Storage: 1 |
| qml/pages/settings/SettingsHistoryDataTab.qml | Storage: 5 |
| qml/pages/settings/SettingsLanguageTab.qml | App: 2 |
| qml/pages/settings/SettingsMachineTab.qml | App: 2 |
| src/ai/aiconversation.cpp | AI: 25 |
| src/ai/aimanager.cpp | AI: 25, BEANBASE: 1 |
| src/ai/aiprovider.cpp | AI: 28 |
| src/ai/dialing_blocks.cpp | AI: 15 |
| src/ai/livesteamcoach.cpp | STEAM: 5 |
| src/ai/profileshapeindex.cpp | PROFILES: 7 |
| src/ai/shotsummarizer_kb.cpp | AI: 10 |
| src/ble/blemanager.cpp | existing forwarding/low-level exception: 2 |
| src/controllers/autoflowcalclassifier.cpp | CALIBRATION: 1 |
| src/controllers/maincontroller.cpp | APP: 5, AUTOLOAD: 6, DE1: 11, NETWORK: 1, RECIPES: 18, SHOT: 18, STEAM: 7, STORAGE: 16, VISUALIZER: 15 |
| src/controllers/profilemanager.cpp | PROFILES: 143 |
| src/controllers/shottimingcontroller.cpp | SHOT: 2 |
| src/core/autowakemanager.cpp | AUTOSLEEP: 6 |
| src/core/batterymanager.cpp | BATTERY: 20 |
| src/core/contextsingletons_qml.h | APP: 3 |
| src/core/crashhandler.cpp | APP: 2, MEMORY: 3 |
| src/core/databasebackupmanager.cpp | STORAGE: 69 |
| src/core/datamigrationclient.cpp | STORAGE: 54 |
| src/core/dbutils.h | STORAGE: 6 |
| src/core/documentformatter.cpp | APP: 6 |
| src/core/emojiassets.cpp | APP: 1 |
| src/core/markdownrenderer.cpp | APP: 1 |
| src/core/memorymonitor.cpp | FONT: 1, MEMORY: 8 |
| src/core/profilestorage.cpp | PROFILES: 21 |
| src/core/settings.cpp | APP: 33 |
| src/core/settings_app.cpp | APP: 5 |
| src/core/settings_brew.cpp | APP: 12 |
| src/core/settings_calibration.cpp | CALIBRATION: 7 |
| src/core/settings_dye.cpp | APP: 2 |
| src/core/settings_network.cpp | APP: 8 |
| src/core/settingsserializer.cpp | STORAGE: 6 |
| src/core/settingsstoremigration.cpp | STORAGE: 6 |
| src/core/translationmanager.cpp | APP: 140 |
| src/core/widgetlibrary.cpp | APP: 34 |
| src/history/coffeebagstorage.cpp | BEANBASE: 38 |
| src/history/recipepromotion.cpp | RECIPES: 2 |
| src/history/recipestorage.cpp | RECIPES: 59 |
| src/history/shotfileparser.cpp | STORAGE: 4 |
| src/history/shothistoryexporter.cpp | STORAGE: 6 |
| src/history/shothistorystorage.cpp | STORAGE: 262 |
| src/history/shothistorystorage_internal.cpp | STORAGE: 3 |
| src/history/shothistorystorage_queries.cpp | STORAGE: 26 |
| src/history/shotimporter.cpp | STORAGE: 18 |
| src/history/shotprojection.cpp | STORAGE: 1 |
| src/history/unifiedbeansearchmodel.cpp | BEANBASE: 1 |
| src/machine/machinestate.cpp | DE1: 1, SCALE: 15, SHOT: 5, STEAM: 4 |
| src/machine/steamhealthtracker.cpp | STEAM: 12 |
| src/machine/stepexitarbiter.cpp | SHOT: 5 |
| src/machine/weightprocessor.cpp | SHOT: 1 |
| src/main.cpp | APP: 40, BLUETOOTH: 3, DE1: 7, SCALE: 20 |
| src/models/flowcalibrationmodel.cpp | CALIBRATION: 4 |
| src/models/shotdatamodel.cpp | SHOT: 8 |
| src/models/steamdatamodel.cpp | STEAM: 1 |
| src/network/crashreporter.cpp | APP: 5 |
| src/network/librarysharing.cpp | APP: 32 |
| src/network/locationprovider.cpp | APP: 38 |
| src/network/mqttclient.cpp | NETWORK: 37 |
| src/network/relayclient.cpp | NETWORK: 15 |
| src/network/screencaptureservice.cpp | APP: 3 |
| src/network/shotreporter.cpp | APP: 12 |
| src/network/shotserver.cpp | NETWORK: 80 |
| src/network/shotserver_auth.cpp | NETWORK: 3 |
| src/network/shotserver_backup.cpp | NETWORK: 28 |
| src/network/shotserver_layout.cpp | NETWORK: 10 |
| src/network/shotserver_settings.cpp | NETWORK: 8 |
| src/network/shotserver_upload.cpp | NETWORK: 24 |
| src/network/visualizerimporter.cpp | VISUALIZER: 44 |
| src/network/visualizeruploader.cpp | VISUALIZER: 62 |
| src/network/webdebuglogger.cpp | RUNTIME: 9, existing forwarding/low-level exception: 4 |
| src/profile/de1apptclfields.cpp | PROFILES: 1 |
| src/profile/profile.cpp | PROFILES: 25 |
| src/profile/profile.h | PROFILES: 6 |
| src/profile/profileconverter.cpp | PROFILES: 12 |
| src/profile/profileframe.cpp | PROFILES: 6 |
| src/profile/profileimporter.cpp | PROFILES: 18 |
| src/profile/profilesavehelper.cpp | PROFILES: 21 |
| src/profile/recipeanalyzer.cpp | PROFILES: 7 |
| src/profile/recipegenerator.cpp | PROFILES: 4 |
| src/rendering/fastlinerenderer.cpp | APP: 2 |
| src/ui/jscanvaspainteritem.cpp | APP: 2 |
| src/widget/machinestatussnapshot.cpp | APP: 2 |

## Native and category follow-up

| Native file | Initial calls / owner |
| --- | --- |
| android/src/io/github/kulitorum/decenza_de1/AndroidUsbScale.java | Scale: 23 |
| android/src/io/github/kulitorum/decenza_de1/AndroidUsbSerial.java | DE1: 20 |
| android/src/io/github/kulitorum/decenza_de1/ApkInstaller.java | App: 15 |
| android/src/io/github/kulitorum/decenza_de1/BleConnectionService.java | Bluetooth: 8 |
| android/src/io/github/kulitorum/decenza_de1/BleHelper.java | Memory: 4 |
| android/src/io/github/kulitorum/decenza_de1/DecenzaActivity.java | App: 9, crash writer: 6 |
| android/src/io/github/kulitorum/decenza_de1/DeviceShutdownService.java | App: 21 |
| android/src/io/github/kulitorum/decenza_de1/MachineStatusWidget.java | App: 2 |
| android/src/io/github/kulitorum/decenza_de1/MachineStatusWidgetProvider.java | App: 2 |
| android/src/io/github/kulitorum/decenza_de1/StorageHelper.java | Storage: 52 |
| android/src/io/github/kulitorum/decenza_de1/UpdateRelaunchReceiver.java | App: 9 |
| android/src/io/github/kulitorum/decenza_de1/UsbHotplugReceiver.java | App: 3 |
| android/src/io/github/kulitorum/decenza_de1/WifiScaleNsdHelper.java | Scale: 13 |

Java calls use DiagnosticLog; six DecenzaActivity uncaught-exception records retain
logcat/crash-file output. iOS widget diagnostics use App. Firmware category calls
use the DE1 firmware owner while retaining qC* category enablement. BLE settings
receipts use App; accessibility settings use Accessibility. The profile editor's
recipe-generation details use Profiles, while drink activation uses Recipes.
Screensaver editor diagnostics use Screensaver.

## Concrete exceptions and non-runtime output

- `src/core/asynclogger.cpp`: the terminal stderr/logcat sink drains the Qt handler;
  feeding it back into Qt would recurse.
- `src/core/crashhandler.cpp`: signal/abort report writers use their independent
  crash-safe file/console path. Memory/FD census is ordinary runtime logging and
  is migrated, with dump/reason on every DEBUG row.
- `src/network/webdebuglogger.cpp` and `src/core/diagnosticlogging.h`: the shared
  QML/native and category formatters validate or apply the registered owner.
- `android/.../DiagnosticLog.java`: bootstrap receivers/services have no Qt logger;
  they write prefixed physical logcat lines until Qt can accept the record.
- `android/.../DecenzaActivity.java`: uncaught-exception handling writes logcat and
  the separate Java crash report. `printStackTrace(PrintWriter)` serializes that
  report, not the runtime log.
- `MemoryMonitor::fullSnapshot` returns a download report rather than emitting
  runtime records; its section formatting remains intentional.
- Tests, standalone developer tools outside runtime roots, generated sources and
  third-party libraries are outside the first-party emitter migration.

No raw printf/puts/iostream/NSLogv/os_log_with_type/qErrnoWarning calls were found
in the final supplemental lexical scan. Gate fixtures cover future raw-output
regressions. Framework diagnostics are preserved as Runtime with actual context;
this capture fallback does not waive emitter conformance.
