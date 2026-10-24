cmake_minimum_required(VERSION 3.21)
project(QtWasmMediaLifecycleRepro LANGUAGES CXX)

# Use the workspace-built stock Qt Multimedia package for this Wasm kit. This
# must be configured with the same wasm_singlethread kit as src/webclient.
set(DECENZA_QTMULTIMEDIA_PREFIX "${CMAKE_SOURCE_DIR}/../../../build/qtmultimedia-wasm-install"
    CACHE PATH "Stock Qt Multimedia install prefix for the WebAssembly kit")
if(DECENZA_QTMULTIMEDIA_PREFIX)
    get_filename_component(DECENZA_QTMULTIMEDIA_PREFIX "${DECENZA_QTMULTIMEDIA_PREFIX}" ABSOLUTE)
    list(PREPEND QT_ADDITIONAL_PACKAGES_PREFIX_PATH "${DECENZA_QTMULTIMEDIA_PREFIX}")
    list(PREPEND CMAKE_PREFIX_PATH "${DECENZA_QTMULTIMEDIA_PREFIX}/lib/cmake")
    list(PREPEND CMAKE_FIND_ROOT_PATH "${DECENZA_QTMULTIMEDIA_PREFIX}")
endif()

find_package(Qt6 6.12.0 EXACT REQUIRED COMPONENTS Quick Qml Multimedia)
find_package(Qt6quickmultimediaplugin 6.12.0 EXACT CONFIG REQUIRED
    PATHS "${DECENZA_QTMULTIMEDIA_PREFIX}/lib/cmake/Qt6Qml/QmlPlugins" NO_DEFAULT_PATH)
qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(qt-wasm-media-lifecycle-repro
    main.cpp
)

qt_add_qml_module(qt-wasm-media-lifecycle-repro
    URI QtWasmMediaLifecycleRepro
    VERSION 1.0
    QML_FILES Main.qml
    SOURCES diagnostics.cpp diagnostics.h
)

target_link_libraries(qt-wasm-media-lifecycle-repro PRIVATE
    Qt6::Quick
    Qt6::Qml
    Qt6::Multimedia
)
