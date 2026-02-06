cmake_minimum_required(VERSION 3.21)
project(Decenza_DE1 VERSION 1.3.8 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

# Version code - always increments for Android (separate from display version)
set(VERSION_CODE_FILE "${CMAKE_SOURCE_DIR}/versioncode.txt")
set(VERSION_HEADER "${CMAKE_BINARY_DIR}/version.h")

# Read version code at configure time
file(READ "${VERSION_CODE_FILE}" CURRENT_VERSION_CODE)
string(STRIP "${CURRENT_VERSION_CODE}" CURRENT_VERSION_CODE)
# Build will increment, so compute next version code for Android
math(EXPR NEXT_VERSION_CODE "${CURRENT_VERSION_CODE} + 1")

# Configure version.h from template
configure_file(
    "${CMAKE_SOURCE_DIR}/src/version.h.in"
    "${VERSION_HEADER}"
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

# Fetch Eclipse Paho MQTT C library (industry-standard, works with FetchContent)
include(FetchContent)
FetchContent_Declare(
    paho_mqtt_c
    GIT_REPOSITORY https://github.com/eclipse/paho.mqtt.c.git
    GIT_TAG        v1.3.13
)
set(PAHO_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(PAHO_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(PAHO_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(PAHO_WITH_SSL OFF CACHE BOOL "" FORCE)
set(PAHO_HIGH_PERFORMANCE OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(paho_mqtt_c)

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
    # Qt 6.10.1 was built for iOS 17.0
    set(CMAKE_OSX_DEPLOYMENT_TARGET "17.0")
    # Fix Qt/Foundation header conflict by compiling all C++ as Objective-C++
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -x objective-c++")
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
    src/machine/machinestate.cpp
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
    src/screensaver/pipegeometry.cpp
    src/screensaver/strangeattractorrenderer.cpp
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
    src/network/shotserver.cpp
    src/network/mqttclient.cpp
    src/network/webdebuglogger.cpp
    src/network/locationprovider.cpp
    src/network/shotreporter.cpp
    src/network/crashreporter.cpp
    src/core/settingsserializer.cpp
    src/core/datamigrationclient.cpp
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

# Android-specific native BLE transport
if(ANDROID)
    list(APPEND SOURCES
        src/ble/transport/androidscalebletransport.cpp
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
    src/ble/de1device.h
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
    src/machine/machinestate.h
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
    src/screensaver/pipegeometry.h
    src/screensaver/strangeattractorrenderer.h
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
    src/network/shotserver.h
    src/network/mqttclient.h
    src/network/webdebuglogger.h
    src/network/locationprovider.h
    src/network/shotreporter.h
    src/network/crashreporter.h
    src/core/settingsserializer.h
    src/core/datamigrationclient.h
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

# Android-specific native BLE transport header
if(ANDROID)
    list(APPEND HEADERS
        src/ble/transport/androidscalebletransport.h
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

# Main executable
qt_add_executable(Decenza_DE1
    ${SOURCES}
    ${HEADERS}
    ${RESOURCES}
)

# Link libraries
target_link_libraries(Decenza_DE1 PRIVATE
    Qt6::Core
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
    paho-mqtt3a-static
)

# Optionally link Quick3D
if(ENABLE_QUICK3D AND Qt6Quick3D_FOUND)
    target_link_libraries(Decenza_DE1 PRIVATE Qt6::Quick3D)
endif()

# iOS/macOS: Use Darwin multimedia plugin (AVFoundation-based)
if(IOS OR APPLE)
    qt_import_plugins(Decenza_DE1
        INCLUDE_BY_TYPE multimedia Qt6::QDarwinMediaPlugin
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
    qml/pages/ShotDetailPage.qml
    qml/pages/ShotComparisonPage.qml
    qml/pages/ProfileInfoPage.qml
    qml/pages/settings/SettingsBluetoothTab.qml
    qml/pages/settings/SettingsPreferencesTab.qml
    qml/pages/settings/SettingsOptionsTab.qml
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
    qml/components/ValueInput.qml
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
    qml/components/FocusIndicator.qml
    qml/components/ColorWheel.qml
    qml/components/ColorEditor.qml
    qml/components/ColorSwatch.qml
    qml/components/StyledTextField.qml
    qml/components/SuggestionField.qml
    qml/components/KeyboardAwareContainer.qml
    qml/components/AIConversationPanel.qml
    qml/components/Tr.qml
    qml/components/RecipeSection.qml
    qml/components/RecipeRow.qml
    qml/components/PresetButton.qml
    qml/components/AccessibleLabel.qml
    qml/components/ComparisonGraph.qml
    qml/components/HistoryShotGraph.qml
    qml/components/StyledSwitch.qml
    qml/components/StyledComboBox.qml
    qml/components/StyledIconButton.qml
    qml/components/StyledTabButton.qml
    qml/components/FlipClockScreensaver.qml
    qml/components/StrangeAttractorScreensaver.qml
    qml/components/UnsavedChangesDialog.qml
    qml/components/SwipeableArea.qml
    qml/components/CrashReportDialog.qml

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
    qml/components/layout/items/TextItem.qml
    qml/components/layout/items/WeatherItem.qml
    qml/components/layout/items/PageTitleItem.qml
    qml/components/layout/items/SteamTemperatureItem.qml
    qml/components/layout/items/SeparatorItem.qml
    qml/components/layout/TextEditorPopup.qml
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
        qml/components/ShotMapScreensaver.qml
    )
endif()

qt_add_qml_module(Decenza_DE1
    URI DecenzaDE1
    VERSION 1.1
    QML_FILES ${QML_FILES}
)

# Increment version code on every build
add_custom_target(increment_version_code ALL
    COMMAND ${CMAKE_COMMAND}
        -DVERSION_CODE_FILE=${VERSION_CODE_FILE}
        -DMANIFEST_FILE=${CMAKE_SOURCE_DIR}/android/AndroidManifest.xml
        -DVERSION_STRING=${PROJECT_VERSION}
        -DCMAKE_SOURCE_DIR=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/cmake/IncrementVersionCode.cmake
    COMMENT "Incrementing version code..."
)
add_dependencies(Decenza_DE1 increment_version_code)

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
        QT_ANDROID_VERSION_CODE "${NEXT_VERSION_CODE}"
        QT_ANDROID_VERSION_NAME "${PROJECT_VERSION}"
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
        MACOSX_BUNDLE_BUNDLE_VERSION "${NEXT_VERSION_CODE}"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "io.github.kulitorum.decenza"
        XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon"
        XCODE_ATTRIBUTE_MARKETING_VERSION "${CMAKE_PROJECT_VERSION}"
        XCODE_ATTRIBUTE_CURRENT_PROJECT_VERSION "${NEXT_VERSION_CODE}"
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
