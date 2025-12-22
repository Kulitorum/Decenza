cmake_minimum_required(VERSION 3.21)
project(de1-qt VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Qt 6 modules
find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Qml
    Quick
    QuickControls2
    Bluetooth
    Charts
    Svg
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
    set(ANDROID_MIN_SDK_VERSION 26)
    set(ANDROID_TARGET_SDK_VERSION 34)
endif()

if(IOS)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0")
endif()

# Source files
set(SOURCES
    src/main.cpp
    src/core/settings.cpp
    src/ble/protocol/binarycodec.cpp
    src/ble/blemanager.cpp
    src/ble/de1device.cpp
    src/ble/scaledevice.cpp
    src/ble/scales/decentscale.cpp
    src/machine/machinestate.cpp
    src/profile/profile.cpp
    src/profile/profileframe.cpp
    src/models/shotdatamodel.cpp
    src/controllers/maincontroller.cpp
)

set(HEADERS
    src/core/settings.h
    src/ble/protocol/binarycodec.h
    src/ble/protocol/de1characteristics.h
    src/ble/blemanager.h
    src/ble/de1device.h
    src/ble/scaledevice.h
    src/ble/scales/decentscale.h
    src/machine/machinestate.h
    src/profile/profile.h
    src/profile/profileframe.h
    src/models/shotdatamodel.h
    src/controllers/maincontroller.h
)

# Resources
qt_add_resources(RESOURCES resources/resources.qrc)

# Main executable
qt_add_executable(de1-qt
    ${SOURCES}
    ${HEADERS}
    ${RESOURCES}
)

# Link libraries
target_link_libraries(de1-qt PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Bluetooth
    Qt6::Charts
    Qt6::Svg
)

# Include directories
target_include_directories(de1-qt PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# QML module - mark Theme.qml as singleton
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(de1-qt
    URI DE1App
    VERSION 1.0
    QML_FILES
        qml/main.qml
        qml/Theme.qml
        qml/pages/IdlePage.qml
        qml/pages/EspressoPage.qml
        qml/pages/SteamPage.qml
        qml/pages/HotWaterPage.qml
        qml/pages/SettingsPage.qml
        qml/components/ActionButton.qml
        qml/components/CircularGauge.qml
        qml/components/ConnectionIndicator.qml
        qml/components/ShotGraph.qml
        qml/components/StatusBar.qml
)

# Android deployment
if(ANDROID)
    set_target_properties(de1-qt PROPERTIES
        QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android"
    )
endif()

# iOS deployment
if(IOS)
    set_target_properties(de1-qt PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/ios/Info.plist"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.decentespresso.de1qt"
    )
endif()
