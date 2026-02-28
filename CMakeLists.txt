cmake_minimum_required(VERSION 3.21)
project(Decenza_DE1 VERSION 1.5.6 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

# Version code - incremented by CI on tag push (separate from display version)
set(VERSION_CODE_FILE "${CMAKE_SOURCE_DIR}/versioncode.txt")
set(VERSION_HEADER "${CMAKE_BINARY_DIR}/version.h")
set(VERSION_CODE_CPP "${CMAKE_BINARY_DIR}/version_code.cpp")

# Read version code at configure time (CI bumps versioncode.txt before configure)
file(READ "${VERSION_CODE_FILE}" CURRENT_VERSION_CODE)
string(STRIP "${CURRENT_VERSION_CODE}" CURRENT_VERSION_CODE)

# Configure version.h from template (stable values: VERSION_STRING, VERSION_MAJOR, etc.)
configure_file(
    "${CMAKE_SOURCE_DIR}/src/version.h.in"
    "${VERSION_HEADER}"
    @ONLY
)

# Generate initial version_code.cpp (updated every build by ApplyVersionCode.cmake)
configure_file(
    "${CMAKE_SOURCE_DIR}/src/version_code.cpp.in"
    "${VERSION_CODE_CPP}"
    @ONLY
)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Optional features (Quick3D not available on all platforms, e.g. Raspberry Pi)
option(ENABLE_QUICK3D "Enable Qt Quick3D for 3D screensavers" ON)

# Qt 6 modules - core required components
find_package(Qt6 REQUIRED COMPONENTS
    Core
    CorePrivate
    Gui
    Widgets
    Qml
    Quick
    QuickControls2
    Bluetooth
    Charts
    Svg
    Multimedia
    TextToSpeech
    Network
    Sql
    Positioning
)

# Optional: ShaderTools (for GPU CRT/Pip-Boy shader effect)
find_package(Qt6 QUIET COMPONENTS ShaderTools)
if(Qt6ShaderTools_FOUND)
    message(STATUS "Qt6ShaderTools found - GPU shader effects enabled")
else()
    message(STATUS "Qt6ShaderTools not found - GPU shader effects disabled (Canvas fallback)")
endif()

# Optional: Quick3D (only used for screensavers)
if(ENABLE_QUICK3D)
    find_package(Qt6 QUIET COMPONENTS Quick3D)
    if(Qt6Quick3D_FOUND)
        message(STATUS "Qt6Quick3D found - 3D screensavers enabled")
        add_compile_definitions(HAVE_QUICK3D)
    else()
        message(STATUS "Qt6Quick3D not found - 3D screensavers disabled")
        set(ENABLE_QUICK3D OFF)
    endif()
endif()

# Silence Qt policy warnings (qt_policy introduced in Qt 6.5)
if(Qt6_VERSION VERSION_GREATER_EQUAL "6.5.0")
    qt_policy(SET QTP0001 NEW)
    qt_policy(SET QTP0004 NEW)
endif()

# Enable QML debugging in debug builds
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(QT_QML_DEBUG)
endif()

# Platform-specific settings
if(ANDROID)
    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    set(ANDROID_MIN_SDK_VERSION 28)
    set(ANDROID_TARGET_SDK_VERSION 34)

    # Fetch OpenSSL for Android (required for HTTPS)
    # See: https://github.com/KDAB/android_openssl
    include(FetchContent)
    FetchContent_Declare(
        android_openssl
        DOWNLOAD_EXTRACT_TIMESTAMP true
        URL https://github.com/KDAB/android_openssl/archive/refs/heads/master.zip
    )
    FetchContent_MakeAvailable(android_openssl)
    include(${android_openssl_SOURCE_DIR}/android_openssl.cmake)
endif()

if(IOS)
    # Qt 6.10.2 was built for iOS 17.0
    set(CMAKE_OSX_DEPLOYMENT_TARGET "17.0")
    # Fix Qt/Foundation header conflict by compiling all C++ as Objective-C++
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -x objective-c++")
endif()

# OpenSSL for HTTPS/TLS and self-signed certificate generation
# Android uses KDAB android_openssl (fetched above), iOS doesn't need it (no web server)
if(NOT ANDROID AND NOT IOS)
    find_package(OpenSSL REQUIRED)
endif()

# Fetch Eclipse Paho MQTT C library (industry-standard, works with FetchContent)
# OpenSSL must be found BEFORE this block so Paho can link against it.
# PAHO_WITH_SSL ON makes Paho use OpenSSL's SHA1 instead of bundling its own,
# which avoids duplicate SHA1_Init/Final/Update symbols on macOS when both
# Paho and OpenSSL are linked as static libraries.
include(FetchContent)
FetchContent_Declare(
    paho_mqtt_c
    GIT_REPOSITORY https://github.com/eclipse/paho.mqtt.c.git
    GIT_TAG        v1.3.13
)
set(PAHO_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(PAHO_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(PAHO_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
if(NOT ANDROID AND NOT IOS)
    # Desktop: use OpenSSL's SHA1 to avoid duplicate symbols with libcrypto.a
    set(PAHO_WITH_SSL ON CACHE BOOL "" FORCE)
else()
    # Android: KDAB provides OpenSSL separately; iOS: no OpenSSL at all
    set(PAHO_WITH_SSL OFF CACHE BOOL "" FORCE)
endif()
set(PAHO_HIGH_PERFORMANCE OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(paho_mqtt_c)

# Address Sanitizer - enabled for single-config Debug builds (Makefiles, Ninja).
# Multi-config generators (Visual Studio, Xcode) don't set CMAKE_BUILD_TYPE at
# configure time, so flags would leak into Release builds. Android requires special
# ASan packaging (wrap.sh + shared lib in APK) that isn't handled here.
if(NOT CMAKE_CONFIGURATION_TYPES AND CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT ANDROID AND NOT APPLE)
    if(MSVC)
        message(STATUS ">>> ASan ENABLED for debug build (MSVC) <<<")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /fsanitize=address")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /fsanitize=address")
    else()
        message(STATUS ">>> ASan ENABLED for debug build <<<")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fsanitize=address")
    endif()
endif()

# Enable parallel builds
if(NOT DEFINED CMAKE_BUILD_PARALLEL_LEVEL)
    include(ProcessorCount)
    ProcessorCount(N)
    if(N GREATER 0)
        set(CMAKE_BUILD_PARALLEL_LEVEL ${N})
    endif()
endif()

# Source files
set(SOURCES
    src/main.cpp
    src/core/settings.cpp
    src/core/widgetlibrary.cpp
    src/core/batterymanager.cpp
    src/core/accessibilitymanager.cpp
    src/core/autowakemanager.cpp
    src/core/crashhandler.cpp
    src/core/profilestorage.cpp
    src/core/translationmanager.cpp
    src/core/updatechecker.cpp
    src/ble/protocol/binarycodec.cpp
    src/ble/blemanager.cpp
    src/ble/de1device.cpp
    src/ble/bletransport.cpp
    src/ble/scaledevice.cpp
    src/ble/scales/scalefactory.cpp
    src/ble/scales/decentscale.cpp
    src/ble/scales/acaiascale.cpp
    src/ble/scales/felicitascale.cpp
    src/ble/scales/skalescale.cpp
    src/ble/scales/hiroiascale.cpp
    src/ble/scales/bookooscale.cpp
    src/ble/scales/smartchefscale.cpp
    src/ble/scales/difluidscale.cpp
    src/ble/scales/eurekaprecisascale.cpp
    src/ble/scales/solobaristascale.cpp
    src/ble/scales/atomhearteclairscale.cpp
    src/ble/scales/variaakuscale.cpp
    src/ble/scales/flowscale.cpp
    src/ble/transport/qtscalebletransport.cpp
    src/core/asynclogger.cpp
    src/machine/machinestate.cpp
    src/machine/weightprocessor.cpp
    src/profile/profile.cpp
    src/profile/profileframe.cpp
    src/profile/profileconverter.cpp
    src/profile/profileimporter.cpp
    src/profile/recipeparams.cpp
    src/profile/recipegenerator.cpp
    src/profile/recipeanalyzer.cpp
    src/models/shotdatamodel.cpp
    src/controllers/maincontroller.cpp
    src/controllers/directcontroller.cpp
    src/controllers/shottimingcontroller.cpp
    src/screensaver/screensavervideomanager.cpp
    src/screensaver/strangeattractorrenderer.cpp
    src/rendering/fastlinerenderer.cpp
    src/network/visualizeruploader.cpp
    src/network/visualizerimporter.cpp
    src/ai/aimanager.cpp
    src/ai/aiprovider.cpp
    src/ai/aiconversation.cpp
    src/ai/shotsummarizer.cpp
    src/history/shothistorystorage.cpp
    src/history/shotdebuglogger.cpp
    src/history/shotfileparser.cpp
    src/history/shotimporter.cpp
    src/models/shotcomparisonmodel.cpp
    src/models/flowcalibrationmodel.cpp
    src/network/shotserver.cpp
    src/network/shotserver_shots.cpp
    src/network/shotserver_upload.cpp
    src/network/shotserver_backup.cpp
    src/network/shotserver_layout.cpp
    src/network/shotserver_theme.cpp
    src/network/shotserver_settings.cpp
    src/network/shotserver_auth.cpp
    src/network/shotserver_ai.cpp
    src/network/mqttclient.cpp
    src/network/webdebuglogger.cpp
    src/network/locationprovider.cpp
    src/network/shotreporter.cpp
    src/network/crashreporter.cpp
    src/network/librarysharing.cpp
    src/core/settingsserializer.cpp
    src/core/datamigrationclient.cpp
    src/core/databasebackupmanager.cpp
    src/core/documentformatter.cpp
    src/weather/weathermanager.cpp
)

# Simulator files - Windows Debug or macOS
if((WIN32 OR APPLE) AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    list(APPEND SOURCES
        src/simulator/ghcsimulator.cpp
        src/simulator/de1simulator.cpp
        src/simulator/simulatedscale.cpp
    )
endif()

# Native CoreBluetooth transport (iOS and macOS)
if(APPLE)
    list(APPEND SOURCES
        src/ble/transport/corebluetooth/corebluetoothscalebletransport.mm
    )
endif()

set(HEADERS
    src/core/settings.h
    src/core/widgetlibrary.h
    src/core/batterymanager.h
    src/core/accessibilitymanager.h
    src/core/autowakemanager.h
    src/core/crashhandler.h
    src/core/profilestorage.h
    src/core/translationmanager.h
    src/core/updatechecker.h
    src/ble/protocol/binarycodec.h
    src/ble/protocol/de1characteristics.h
    src/ble/blemanager.h
    src/ble/de1transport.h
    src/ble/de1device.h
    src/ble/bletransport.h
    src/ble/scaledevice.h
    src/ble/scales/scalefactory.h
    src/ble/scales/decentscale.h
    src/ble/scales/acaiascale.h
    src/ble/scales/felicitascale.h
    src/ble/scales/skalescale.h
    src/ble/scales/hiroiascale.h
    src/ble/scales/bookooscale.h
    src/ble/scales/smartchefscale.h
    src/ble/scales/difluidscale.h
    src/ble/scales/eurekaprecisascale.h
    src/ble/scales/solobaristascale.h
    src/ble/scales/atomhearteclairscale.h
    src/ble/scales/variaakuscale.h
    src/ble/scales/flowscale.h
    src/ble/transport/scalebletransport.h
    src/ble/transport/qtscalebletransport.h
    src/core/asynclogger.h
    src/machine/machinestate.h
    src/machine/weightprocessor.h
    src/profile/profile.h
    src/profile/profileframe.h
    src/profile/profileconverter.h
    src/profile/profileimporter.h
    src/profile/recipeparams.h
    src/profile/recipegenerator.h
    src/profile/recipeanalyzer.h
    src/models/shotdatamodel.h
    src/controllers/maincontroller.h
    src/controllers/directcontroller.h
    src/controllers/shottimingcontroller.h
    src/screensaver/screensavervideomanager.h
    src/screensaver/strangeattractorrenderer.h
    src/rendering/fastlinerenderer.h
    src/network/visualizeruploader.h
    src/network/visualizerimporter.h
    src/ai/aimanager.h
    src/ai/aiprovider.h
    src/ai/shotsummarizer.h
    src/history/shothistorystorage.h
    src/history/shotdebuglogger.h
    src/history/shotfileparser.h
    src/history/shotimporter.h
    src/models/shotcomparisonmodel.h
    src/models/flowcalibrationmodel.h
    src/network/shotserver.h
    src/network/mqttclient.h
    src/network/webdebuglogger.h
    src/network/locationprovider.h
    src/network/shotreporter.h
    src/network/crashreporter.h
    src/network/librarysharing.h
    src/core/settingsserializer.h
    src/core/datamigrationclient.h
    src/core/databasebackupmanager.h
    src/core/documentformatter.h
    src/weather/weathermanager.h
)

# Simulator headers - Windows Debug or macOS
if((WIN32 OR APPLE) AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    list(APPEND HEADERS
        src/simulator/ghcsimulator.h
        src/simulator/de1simulator.h
        src/simulator/simulatedscale.h
    )
endif()

# Native CoreBluetooth transport header (iOS and macOS)
if(APPLE)
    list(APPEND HEADERS
        src/ble/transport/corebluetooth/corebluetoothscalebletransport.h
    )
endif()

# Resources
qt_add_resources(RESOURCES resources/resources.qrc)
qt_add_resources(RESOURCES resources/emoji.qrc)

# Main executable
qt_add_executable(Decenza_DE1
    ${SOURCES}
    ${HEADERS}
    ${RESOURCES}
)

# Link libraries
target_link_libraries(Decenza_DE1 PRIVATE
    Qt6::Core
    Qt6::CorePrivate
    Qt6::Gui
    Qt6::Widgets
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Bluetooth
    Qt6::Charts
    Qt6::Svg
    Qt6::Multimedia
    Qt6::TextToSpeech
    Qt6::Network
    Qt6::Sql
    Qt6::Positioning
)

# Link Paho MQTT - use SSL variant (paho-mqtt3as) on desktop where OpenSSL is
# statically linked (avoids duplicate SHA1 symbols), non-SSL on mobile
if(NOT ANDROID AND NOT IOS)
    target_link_libraries(Decenza_DE1 PRIVATE paho-mqtt3as-static)
else()
    target_link_libraries(Decenza_DE1 PRIVATE paho-mqtt3a-static)
endif()

# Link OpenSSL (non-Android, non-iOS; Android uses add_android_openssl_libraries below)
if(NOT ANDROID AND NOT IOS)
    target_link_libraries(Decenza_DE1 PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif()

# Optionally link Quick3D and add 3D screensaver sources
if(ENABLE_QUICK3D AND Qt6Quick3D_FOUND)
    target_link_libraries(Decenza_DE1 PRIVATE Qt6::Quick3D)
    target_compile_definitions(Decenza_DE1 PRIVATE ENABLE_QUICK3D)
    target_sources(Decenza_DE1 PRIVATE
        src/screensaver/pipegeometry.cpp
        src/screensaver/pipegeometry.h
    )
endif()

# iOS/macOS: Use Darwin multimedia and location permission plugins
if(IOS OR APPLE)
    qt_import_plugins(Decenza_DE1
        INCLUDE_BY_TYPE multimedia Qt6::QDarwinMediaPlugin
        INCLUDE_BY_TYPE permissions Qt6::QDarwinLocationPermissionPlugin
    )
endif()

# Include directories
target_include_directories(Decenza_DE1 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}  # For version.h
)

# QML module - mark Theme.qml as singleton
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

# Define QML files list for dependency tracking
set(QML_FILES
    qml/main.qml
    qml/Theme.qml
    qml/pages/IdlePage.qml
    qml/pages/EspressoPage.qml
    qml/pages/SteamPage.qml
    qml/pages/HotWaterPage.qml
    qml/pages/SettingsPage.qml
    qml/pages/ProfileEditorPage.qml
    qml/pages/RecipeEditorPage.qml
    qml/pages/SimpleProfileEditorPage.qml
    qml/pages/PressureEditorPage.qml
    qml/pages/FlowEditorPage.qml
    qml/pages/ProfileSelectorPage.qml
    qml/pages/DescalingPage.qml
    qml/pages/ScreensaverPage.qml
    qml/pages/FlushPage.qml
    qml/pages/VisualizerBrowserPage.qml
    qml/pages/ProfileImportPage.qml
    qml/pages/VisualizerMultiImportPage.qml
    qml/pages/BeanInfoPage.qml
    qml/pages/PostShotReviewPage.qml
    qml/pages/AISettingsPage.qml
    qml/pages/DialingAssistantPage.qml
    qml/pages/ShotHistoryPage.qml
    qml/pages/AutoFavoritesPage.qml
    qml/pages/AutoFavoriteInfoPage.qml
    qml/pages/ShotDetailPage.qml
    qml/pages/ShotComparisonPage.qml
    qml/pages/FlowCalibrationPage.qml
    qml/pages/ProfileInfoPage.qml
    qml/pages/settings/SettingsBluetoothTab.qml
    qml/pages/settings/SettingsPreferencesTab.qml
    qml/pages/settings/SettingsScreensaverTab.qml
    qml/pages/settings/SettingsVisualizerTab.qml
    qml/pages/settings/SettingsAITab.qml
    qml/pages/settings/SettingsAccessibilityTab.qml
    qml/pages/settings/SettingsThemesTab.qml
    qml/pages/settings/SettingsLanguageTab.qml
    qml/pages/settings/SettingsShotHistoryTab.qml
    qml/pages/settings/SettingsDataTab.qml
    qml/pages/settings/SettingsHomeAutomationTab.qml
    qml/pages/settings/SettingsUpdateTab.qml
    qml/pages/settings/SettingsAboutTab.qml
    qml/pages/settings/SettingsDebugTab.qml
    qml/pages/settings/SettingsLayoutTab.qml
    qml/pages/settings/LayoutEditorZone.qml
    qml/pages/settings/AddLanguagePage.qml
    qml/pages/settings/StringBrowserPage.qml
    qml/components/ActionButton.qml
	qml/components/BrewDialog.qml
    qml/components/CircularGauge.qml
    qml/components/ConnectionIndicator.qml
    qml/components/PresetPillRow.qml
    qml/components/TouchSlider.qml
    qml/components/StepSlider.qml
    qml/components/ValueInput.qml
    qml/components/RatingInput.qml
    qml/components/ShotGraph.qml
    qml/components/StatusBar.qml
    qml/components/ShotPlanText.qml
    qml/components/ProfileGraph.qml
    qml/components/ProfilePreviewPopup.qml
    qml/components/ProfileInfoButton.qml
    qml/components/ProfileDisplayButton.qml
    qml/components/BottomBar.qml
    qml/components/AccessibleMouseArea.qml
    qml/components/AccessibleTapHandler.qml
    qml/components/AccessibleButton.qml
    qml/components/ExpandableTextArea.qml
    qml/components/FocusIndicator.qml
    qml/components/ColorWheel.qml
    qml/components/ColorEditor.qml
    qml/components/ColorSwatch.qml
    qml/components/StyledTextField.qml
    qml/components/SuggestionField.qml
    qml/components/KeyboardAwareContainer.qml
    qml/components/AIConversationPanel.qml
    qml/components/ConversationOverlay.qml
    qml/components/Tr.qml
    qml/components/RecipeSection.qml
    qml/components/RecipeRow.qml
    qml/components/PresetButton.qml
    qml/components/AccessibleLabel.qml
    qml/components/ComparisonGraph.qml
    qml/components/HistoryShotGraph.qml
    qml/components/GraphInspectBar.qml
    qml/components/ComparisonInspectBar.qml
    qml/components/ComparisonDataTable.qml
    qml/components/ComparisonShotTable.qml
    qml/components/GraphLegend.qml
    qml/components/StyledSwitch.qml
    qml/components/StyledComboBox.qml
    qml/components/StyledIconButton.qml
    qml/components/StyledTabButton.qml
    qml/components/FlipClockScreensaver.qml
    qml/components/StrangeAttractorScreensaver.qml
    qml/components/ShotMapScreensaver.qml
    qml/components/UnsavedChangesDialog.qml
    qml/components/SwipeableArea.qml
    qml/components/CrashReportDialog.qml
    qml/components/CrtOverlay.qml
    qml/components/CrtShaderEffect.qml
    qml/components/QrCode.qml
    qml/components/qrcode.js

    qml/components/layout/LayoutItemDelegate.qml
    qml/components/layout/LayoutBarZone.qml
    qml/components/layout/LayoutCenterZone.qml
    qml/components/layout/items/EspressoItem.qml
    qml/components/layout/items/SteamItem.qml
    qml/components/layout/items/HotWaterItem.qml
    qml/components/layout/items/FlushItem.qml
    qml/components/layout/items/BeansItem.qml
    qml/components/layout/items/HistoryItem.qml
    qml/components/layout/items/AutoFavoritesItem.qml
    qml/components/layout/items/SleepItem.qml
    qml/components/layout/items/SettingsItem.qml
    qml/components/layout/items/TemperatureItem.qml
    qml/components/layout/items/WaterLevelItem.qml
    qml/components/layout/items/ConnectionStatusItem.qml
    qml/components/layout/items/ScaleWeightItem.qml
    qml/components/layout/items/ShotPlanItem.qml
    qml/components/layout/items/SpacerItem.qml
    qml/components/layout/items/CustomItem.qml
    qml/components/layout/items/WeatherItem.qml
    qml/components/layout/items/PageTitleItem.qml
    qml/components/layout/items/SteamTemperatureItem.qml
    qml/components/layout/items/SeparatorItem.qml
    qml/components/layout/items/QuitItem.qml
    qml/components/layout/items/ScreensaverItem.qml
    qml/components/layout/items/LastShotItem.qml
    qml/components/layout/items/MachineStatusItem.qml
    qml/components/layout/CustomEditorPopup.qml
    qml/components/layout/ScreensaverEditorPopup.qml
    qml/components/layout/EmojiPicker.qml
    qml/components/layout/EmojiData.js
    qml/components/library/LibraryPanel.qml
    qml/components/library/LibraryItemCard.qml
    qml/pages/CommunityBrowserPage.qml
)

# Simulator QML - Windows Debug or macOS
if((WIN32 OR APPLE) AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    list(APPEND QML_FILES
        qml/simulator/GHCSimulatorWindow.qml
    )
endif()

# Quick3D screensavers - only when Quick3D is available
if(ENABLE_QUICK3D AND Qt6Quick3D_FOUND)
    list(APPEND QML_FILES
        qml/components/PipesScreensaver.qml
        qml/components/ShotMapGlobe.qml
    )
endif()

qt_add_qml_module(Decenza_DE1
    URI DecenzaDE1
    VERSION 1.1
    QML_FILES ${QML_FILES}
    NO_GENERATE_EXTRA_QMLDIRS
)

# Compile CRT shader for GPU effect (requires ShaderTools)
if(Qt6ShaderTools_FOUND)
    qt_add_shaders(Decenza_DE1 "crt_shaders"
        BATCHABLE
        PREFIX "/"
        FILES
            shaders/crt.frag
    )
endif()

# Apply version code on every build (reads versioncode.txt as-is, no increment).
# BYPRODUCTS tells Ninja that this target produces version_code.cpp, so dependents
# (the compilation of version_code.cpp into an object file) are scheduled after this
# target and properly detect the file change. This fixes the stale build number issue
# where Ninja's pre-computed dirty state missed header-only changes.
add_custom_target(apply_version_code ALL
    COMMAND ${CMAKE_COMMAND}
        -DVERSION_CODE_FILE=${VERSION_CODE_FILE}
        -DMANIFEST_FILE=${CMAKE_SOURCE_DIR}/android/AndroidManifest.xml
        -DVERSION_STRING=${PROJECT_VERSION}
        -DCMAKE_SOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DVERSION_CODE_CPP=${VERSION_CODE_CPP}
        -DVERSION_CODE_CPP_TEMPLATE=${CMAKE_SOURCE_DIR}/src/version_code.cpp.in
        -DPROJECT_VERSION=${PROJECT_VERSION}
        -DPROJECT_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        -DPROJECT_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        -DPROJECT_VERSION_PATCH=${PROJECT_VERSION_PATCH}
        -DISS_TEMPLATE=${CMAKE_SOURCE_DIR}/installer/version.iss.in
        -DISS_OUTPUT=${CMAKE_SOURCE_DIR}/installer/version.iss
        -P ${CMAKE_SOURCE_DIR}/cmake/ApplyVersionCode.cmake
    BYPRODUCTS ${VERSION_CODE_CPP}
    COMMENT "Applying version code..."
)
add_dependencies(Decenza_DE1 apply_version_code)

# Add generated version_code.cpp to the build (recompiled every build, triggers relink)
target_sources(Decenza_DE1 PRIVATE ${VERSION_CODE_CPP})
set_source_files_properties(${VERSION_CODE_CPP} PROPERTIES GENERATED TRUE)

# Force QML resource rebuild when source files change
# NOTE: Disabled - defeats QML caching which slows page loads
# Re-enable temporarily if QML changes aren't being picked up
# set(QML_RCC_CPP "${CMAKE_CURRENT_BINARY_DIR}/.qt/rcc/qrc_Decenza_DE1_raw_qml_0.cpp")
# add_custom_target(qml_force_rebuild ALL
#     COMMAND ${CMAKE_COMMAND} -E rm -f ${QML_RCC_CPP}
#     COMMENT "Invalidating QML resources for rebuild..."
# )
# add_dependencies(Decenza_DE1 qml_force_rebuild)

# Also add QML files as sources so IDE can find them
set_property(TARGET Decenza_DE1 APPEND PROPERTY SOURCES ${QML_FILES})

# Android deployment
if(ANDROID)
    set_target_properties(Decenza_DE1 PROPERTIES
        QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android"
        # Note: versionCode and versionName are managed by ApplyVersionCode.cmake
        # at build time (updates both AndroidManifest.xml and version_code.cpp in sync).
        # Do NOT set QT_ANDROID_VERSION_CODE here — it freezes at configure time
        # and diverges from version_code.cpp on rebuilds without reconfigure.
    )
    # Add OpenSSL libraries for HTTPS support
    add_android_openssl_libraries(Decenza_DE1)
endif()

# iOS deployment
if(IOS)
    set_target_properties(Decenza_DE1 PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/ios/Info.plist"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${CMAKE_PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${CURRENT_VERSION_CODE}"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "io.github.kulitorum.decenza"
        XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon"
        XCODE_ATTRIBUTE_MARKETING_VERSION "${CMAKE_PROJECT_VERSION}"
        XCODE_ATTRIBUTE_CURRENT_PROJECT_VERSION "${CURRENT_VERSION_CODE}"
        XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT "dwarf-with-dsym"
        XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT[variant=Release] "dwarf-with-dsym"
        XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT[variant=Debug] "dwarf-with-dsym"
        XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT[variant=MinSizeRel] "dwarf-with-dsym"
        XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT[variant=RelWithDebInfo] "dwarf-with-dsym"
        XCODE_ATTRIBUTE_DWARF_DSYM_FOLDER_PATH "$(CONFIGURATION_BUILD_DIR)"
    )
    # Add iOS asset catalog for app icon
    target_sources(Decenza_DE1 PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/ios/Assets.xcassets"
    )
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/ios/Assets.xcassets"
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources
    )
    # Link iOS frameworks required for multimedia (AVFoundation backend)
    target_link_libraries(Decenza_DE1 PRIVATE
        "-framework AVFoundation"
        "-framework CoreMedia"
        "-framework CoreVideo"
        "-framework CoreAudio"
        "-framework AudioToolbox"
    )
    # Link CoreBluetooth for native scale BLE transport
    target_link_libraries(Decenza_DE1 PRIVATE
        "-framework CoreBluetooth"
    )
endif()

# macOS deployment
if(APPLE AND NOT IOS)
    # Link CoreBluetooth for native scale BLE transport on macOS
    target_link_libraries(Decenza_DE1 PRIVATE
        "-framework CoreBluetooth"
    )
    set_target_properties(Decenza_DE1 PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/macos/Info.plist"
        MACOSX_BUNDLE_ICON_FILE "AppIcon.icns"
    )
    # Add macOS app icon to bundle
    set(MACOS_ICON "${CMAKE_CURRENT_SOURCE_DIR}/macos/AppIcon.icns")
    target_sources(Decenza_DE1 PRIVATE ${MACOS_ICON})
    set_source_files_properties(${MACOS_ICON} PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources
    )
endif()

# Windows installer - generate version.iss for Inno Setup
if(WIN32)
    configure_file(
        "${CMAKE_SOURCE_DIR}/installer/version.iss.in"
        "${CMAKE_SOURCE_DIR}/installer/version.iss"
        @ONLY
    )
endif()
