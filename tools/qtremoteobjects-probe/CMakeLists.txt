cmake_minimum_required(VERSION 3.21)
project(QtRemoteObjectsWasmProbe LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(QTRO_PREFIX "" CACHE PATH "Optional QtRemoteObjects install prefix (for the Wasm kit)")
if(QTRO_PREFIX)
    list(PREPEND QT_ADDITIONAL_PACKAGES_PREFIX_PATH "${QTRO_PREFIX}")
endif()

find_package(Qt6 6.12 REQUIRED COMPONENTS Core Gui Network RemoteObjects WebSockets)
qt_standard_project_setup(REQUIRES 6.12)

if(EMSCRIPTEN)
    qt_add_executable(qtro_probe_client
        client/main.cpp
        client/probecontroller.cpp
        client/probecontroller.h
        shared/websocketiodevice.cpp
        shared/websocketiodevice.h
    )
    qt6_add_repc_replicas(qtro_probe_client shared/probe.rep)
    target_include_directories(qtro_probe_client PRIVATE shared client "${CMAKE_CURRENT_BINARY_DIR}")
    target_link_libraries(qtro_probe_client PRIVATE
        Qt6::Core Qt6::Gui Qt6::Network Qt6::RemoteObjects Qt6::WebSockets)
    target_link_options(qtro_probe_client PRIVATE "-sASSERTIONS=1")
else()
    qt_add_executable(qtro_probe_host
        host/main.cpp
        host/probesource.cpp
        host/probesource.h
        host/staticfileserver.cpp
        host/staticfileserver.h
        shared/websocketiodevice.cpp
        shared/websocketiodevice.h
    )
    qt6_add_repc_sources(qtro_probe_host shared/probe.rep)
    target_include_directories(qtro_probe_host PRIVATE shared host "${CMAKE_CURRENT_BINARY_DIR}")
    target_link_libraries(qtro_probe_host PRIVATE
        Qt6::Core Qt6::Gui Qt6::Network Qt6::RemoteObjects Qt6::WebSockets)
endif()
