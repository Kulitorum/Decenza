cmake_minimum_required(VERSION 3.21)
project(Decenza_DE1 VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

# Build number - increment on every build
set(BUILD_NUMBER_FILE "${CMAKE_SOURCE_DIR}/buildnumber.txt")
set(BUILD_INFO_HEADER "${CMAKE_BINARY_DIR}/buildinfo.h")

# Generate initial header at configure time (without incrementing)
if(NOT EXISTS "${BUILD_INFO_HEADER}")
    file(READ "${BUILD_NUMBER_FILE}" CURRENT_BUILD_NUMBER)
    string(STRIP "${CURRENT_BUILD_NUMBER}" CURRENT_BUILD_NUMBER)
    file(WRITE "${BUILD_INFO_HEADER}"
        "#pragma once\n// Auto-generated - do not edit\n#define BUILD_NUMBER ${CURRENT_BUILD_NUMBER}\n#define BUILD_NUMBER_STRING \"${CURRENT_BUILD_NUMBER}\"\n")
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Qt 6 modules
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
)

# Silence Qt policy warnings
qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0004 NEW)

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
    set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0")
endif()

# Source files
set(SOURCES
    src/main.cpp
    src/core/settings.cpp
    src/core/batterymanager.cpp
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
    src/machine/machinestate.cpp
    src/profile/profile.cpp
    src/profile/profileframe.cpp
    src/models/shotdatamodel.cpp
    src/controllers/maincontroller.cpp
    src/controllers/directcontroller.cpp
    src/screensaver/screensavervideomanager.cpp
    src/network/visualizeruploader.cpp
)

set(HEADERS
    src/core/settings.h
    src/core/batterymanager.h
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
    src/machine/machinestate.h
    src/profile/profile.h
    src/profile/profileframe.h
    src/models/shotdatamodel.h
    src/controllers/maincontroller.h
    src/controllers/directcontroller.h
    src/screensaver/screensavervideomanager.h
    src/network/visualizeruploader.h
)

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
)

# Include directories
target_include_directories(Decenza_DE1 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}  # For buildinfo.h
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
    qml/pages/ProfileSelectorPage.qml
    qml/pages/ScreensaverPage.qml
    qml/pages/FlushPage.qml
    qml/components/ActionButton.qml
    qml/components/CircularGauge.qml
    qml/components/ConnectionIndicator.qml
    qml/components/PresetPillRow.qml
    qml/components/TouchSlider.qml
    qml/components/ValueInput.qml
    qml/components/ShotGraph.qml
    qml/components/StatusBar.qml
    qml/components/ProfileGraph.qml
)

qt_add_qml_module(Decenza_DE1
    URI DecenzaDE1
    VERSION 1.0
    QML_FILES ${QML_FILES}
)

# Increment build number on every build
add_custom_target(increment_build_number ALL
    COMMAND ${CMAKE_COMMAND}
        -DBUILD_NUMBER_FILE=${BUILD_NUMBER_FILE}
        -DOUTPUT_HEADER=${BUILD_INFO_HEADER}
        -P ${CMAKE_SOURCE_DIR}/cmake/IncrementBuildNumber.cmake
    BYPRODUCTS ${BUILD_INFO_HEADER}
    COMMENT "Incrementing build number..."
)
add_dependencies(Decenza_DE1 increment_build_number)
set_source_files_properties(src/main.cpp PROPERTIES OBJECT_DEPENDS ${BUILD_INFO_HEADER})

# Force QML resource rebuild when source files change
# Delete the compiled resource file to force rcc regeneration
set(QML_RCC_CPP "${CMAKE_CURRENT_BINARY_DIR}/.qt/rcc/qrc_Decenza_DE1_raw_qml_0.cpp")

add_custom_target(qml_force_rebuild ALL
    COMMAND ${CMAKE_COMMAND} -E rm -f ${QML_RCC_CPP}
    COMMENT "Invalidating QML resources for rebuild..."
)
add_dependencies(Decenza_DE1 qml_force_rebuild)

# Also add QML files as sources so IDE can find them
set_property(TARGET Decenza_DE1 APPEND PROPERTY SOURCES ${QML_FILES})

# Android deployment
if(ANDROID)
    set_target_properties(Decenza_DE1 PROPERTIES
        QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android"
    )
    # Add OpenSSL libraries for HTTPS support
    add_android_openssl_libraries(Decenza_DE1)
endif()

# iOS deployment
if(IOS)
    set_target_properties(Decenza_DE1 PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/ios/Info.plist"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "io.github.kulitorum.decenza_de1"
    )
endif()

# macOS deployment
if(APPLE AND NOT IOS)
    set_target_properties(Decenza_DE1 PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/macos/Info.plist"
    )
endif()
