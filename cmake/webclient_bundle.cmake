# One native Qt Creator Build may produce the matching Wasm bundle before RCC.
# Release CI supplies a verified prebuilt artifact and never runs a child build.
set(DECENZA_WEB_BUNDLE_DIR "" CACHE PATH "Matched Wasm deployment directory")
set(DECENZA_WEB_BUNDLE_MODE "LOCAL" CACHE STRING "LOCAL (build Wasm), PREBUILT (verify artifact), or OFF")
set_property(CACHE DECENZA_WEB_BUNDLE_MODE PROPERTY STRINGS LOCAL PREBUILT OFF)
option(DECENZA_REQUIRE_WEB_BUNDLE "Fail configuration without a matching Wasm deployment (release CI)" OFF)
option(DECENZA_EMBED_WEB_BUNDLE "Embed the verified bundle (OFF retains local macOS resource-directory packaging)" ON)
set(DECENZA_WASM_BUILD_DIR "" CACHE PATH "Optional existing Wasm kit build directory")
set(DECENZA_WASM_QT_DIR "" CACHE PATH "Qt 6.12.0 wasm_singlethread prefix (auto-detected from an existing kit or beside native Qt)")
set(DECENZA_WASM_HOST_QT_DIR "" CACHE PATH "Native Qt 6.12.0 tools prefix for the child Wasm build")
set(DECENZA_WASM_EMSDK_DIR "" CACHE PATH "Activated Emscripten 5.0.5 SDK (auto-detected from existing Wasm kit or EMSDK)")

string(TOUPPER "${DECENZA_WEB_BUNDLE_MODE}" _webclient_mode)
if(_webclient_mode STREQUAL "AUTO")
    set(_webclient_mode LOCAL)
endif()
if(NOT _webclient_mode MATCHES "^(LOCAL|PREBUILT|OFF)$")
    message(FATAL_ERROR "DECENZA_WEB_BUNDLE_MODE must be LOCAL, PREBUILT, or OFF")
endif()
if(DECENZA_REQUIRE_WEB_BUNDLE)
    set(_webclient_mode PREBUILT)
endif()
if(_webclient_mode STREQUAL "OFF")
    return()
endif()
if(NOT DECENZA_EMBED_WEB_BUNDLE AND (NOT APPLE OR IOS OR DECENZA_REQUIRE_WEB_BUNDLE))
    message(FATAL_ERROR "External web client packaging is only for local macOS builds")
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(_webclient_driver "${CMAKE_SOURCE_DIR}/scripts/build_webclient.py")
set(_webclient_config "${CMAKE_BINARY_DIR}/local-webclient-config.json")
if(_webclient_mode STREQUAL "LOCAL")
    # Discovery only reads kit configuration; all compilation happens as visible
    # incremental build steps after configuration, never in execute_process.
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${_webclient_driver}" discover
            --source-dir "${CMAKE_SOURCE_DIR}" --native-build-dir "${CMAKE_BINARY_DIR}"
            --native-qt-dir "${Qt6_DIR}" --cmake "${CMAKE_COMMAND}"
            --build-dir "${DECENZA_WASM_BUILD_DIR}" --bundle-dir "${DECENZA_WEB_BUNDLE_DIR}"
            --wasm-qt "${DECENZA_WASM_QT_DIR}" --host-qt "${DECENZA_WASM_HOST_QT_DIR}"
            --emsdk "${DECENZA_WASM_EMSDK_DIR}" --output "${_webclient_config}"
        RESULT_VARIABLE _webclient_discovery_result ERROR_VARIABLE _webclient_discovery_error)
    if(_webclient_discovery_result EQUAL 0)
        set(_webclient_mode LOCAL)
        file(READ "${_webclient_config}" _webclient_configuration)
        string(JSON _webclient_build GET "${_webclient_configuration}" build)
        string(JSON _webclient_bundle GET "${_webclient_configuration}" bundle)
        set(DECENZA_WASM_BUILD_DIR "${_webclient_build}" CACHE PATH "Optional existing Wasm kit build directory" FORCE)
        set(DECENZA_WEB_BUNDLE_DIR "${_webclient_bundle}" CACHE PATH "Matched Wasm deployment directory" FORCE)
    else()
        message(FATAL_ERROR "Cannot configure the local Wasm dependency: ${_webclient_discovery_error}")
    endif()
endif()

set(_webclient_staged "${CMAKE_BINARY_DIR}/webclient-bundle")
if(_webclient_mode STREQUAL "LOCAL")
    # Fixed names permit first-build generated assets. Optional gzip slots are
    # private empty placeholders unless advertised by the verified manifest.
    set(_webclient_names index.html decenza-web.js decenza-web.wasm qtloader.js
        qt-dialog-accessibility.js package.json decenza-web.js.gz decenza-web.wasm.gz
        qtloader.js.gz qt-dialog-accessibility.js.gz)
    set(_webclient_files)
    foreach(_webclient_name IN LISTS _webclient_names)
        list(APPEND _webclient_files "${_webclient_staged}/${_webclient_name}")
    endforeach()
    set(_webclient_stamp "${CMAKE_BINARY_DIR}/local-webclient.stamp")
    add_custom_command(OUTPUT "${_webclient_stamp}"
        BYPRODUCTS ${_webclient_files} "${DECENZA_WEB_BUNDLE_DIR}/package.json"
        COMMAND "${Python3_EXECUTABLE}" "${_webclient_driver}" build
            --config "${_webclient_config}" --expected-package-id "${DECENZA_PACKAGE_ID}"
            --output-dir "${_webclient_staged}"
        COMMAND "${CMAKE_COMMAND}" -E touch "${_webclient_stamp}"
        DEPENDS "${DECENZA_PACKAGE_ID_HEADER}" "${_webclient_config}" "${_webclient_driver}"
            "${CMAKE_SOURCE_DIR}/scripts/package_webclient.py"
        COMMENT "Building the matching Wasm client before native packaging"
        USES_TERMINAL VERBATIM)
    add_custom_target(decenza-local-webclient DEPENDS "${_webclient_stamp}")
    add_dependencies(Decenza decenza-local-webclient)
    # Qt6CoreMacros.cmake:2609-2625 uses this target dependency for generated
    # resource inputs with Xcode; other generators use the output-file edges.
    set_source_files_properties(${_webclient_files} PROPERTIES GENERATED TRUE
        _qt_resource_target_dependency decenza-local-webclient)
    message(STATUS "Native Build will incrementally build and package Wasm from ${_webclient_build}")
else()
    if(NOT DECENZA_WEB_BUNDLE_DIR)
        message(FATAL_ERROR "PREBUILT/release packaging requires DECENZA_WEB_BUNDLE_DIR")
    endif()
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/package_webclient.py"
            install --source "${DECENZA_WEB_BUNDLE_DIR}"
            --expected-package-id "${DECENZA_PACKAGE_ID}" --output-dir "${_webclient_staged}"
        RESULT_VARIABLE _webclient_result OUTPUT_VARIABLE _webclient_output ERROR_VARIABLE _webclient_error)
    if(NOT _webclient_result EQUAL 0)
        message(FATAL_ERROR "Matching web client verification failed: ${_webclient_error}")
    endif()
    message(STATUS "${_webclient_output}")
    file(GLOB _webclient_inputs CONFIGURE_DEPENDS "${DECENZA_WEB_BUNDLE_DIR}/*")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_webclient_inputs})
    file(GLOB _webclient_files "${_webclient_staged}/*")
endif()

if(NOT DECENZA_EMBED_WEB_BUNDLE)
    return()
endif()
# Qt6CoreMacros.cmake:616-623 expands TARGET_OBJECTS inside rcc pass 2,
# which Xcode cannot represent for universal builds. Use regular compressed
# C++ resources for Xcode (including iOS); retain direct objects elsewhere.
set(_webclient_big_resources)
if(NOT IOS AND NOT CMAKE_GENERATOR STREQUAL "Xcode")
    set(_webclient_big_resources BIG_RESOURCES)
endif()
# Qt6QmlMacros.cmake:3999 makes every QML cache unit depend on the app's
# _qt_generated_qrc_files. Keep this non-QML payload on a separate carrier so
# CI can add it without regenerating all QML. Qt6CoreMacros.cmake:2230-2288
# and 2735-2743 propagate static-library resources into the final executable.
set(_webclient_anchor "${CMAKE_BINARY_DIR}/generated/webclient_resources.cpp")
file(GENERATE OUTPUT "${_webclient_anchor}"
    CONTENT "void decenzaWebclientResourceAnchor() {}\n")
add_library(decenza_webclient_resources STATIC "${_webclient_anchor}")
set_target_properties(decenza_webclient_resources PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_link_libraries(decenza_webclient_resources PRIVATE Qt6::Core)
target_link_libraries(Decenza PRIVATE decenza_webclient_resources)
qt_add_resources(decenza_webclient_resources packaged_webclient
    ${_webclient_big_resources}
    PREFIX "/webclient" BASE "${_webclient_staged}"
    FILES ${_webclient_files} OPTIONS --compress-algo zlib --compress 9)
if(APPLE AND NOT IOS AND DECENZA_REQUIRE_WEB_BUNDLE)
    # CI reads the same resource carrier in each universal binary slice.
    # Qt6CoreMacros.cmake:682-696 defers finalization so the static resource
    # initializer is propagated after target_link_libraries below.
    qt_add_executable(decenza-verify-webclient EXCLUDE_FROM_ALL
        "${CMAKE_SOURCE_DIR}/tools/webclient/verify_embedded_webclient.cpp")
    set_target_properties(decenza-verify-webclient PROPERTIES
        MACOSX_BUNDLE FALSE AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_link_libraries(decenza-verify-webclient PRIVATE decenza_webclient_resources)
endif()
if(TARGET decenza-local-webclient)
    add_dependencies(decenza_webclient_resources decenza-local-webclient)
    if(TARGET decenza_webclient_resources_packaged_webclient_obj)
        add_dependencies(decenza_webclient_resources_packaged_webclient_obj decenza-local-webclient)
    endif()
endif()
# Only this translation unit selects the embedded reader. Keeping the define
# off the target avoids recompiling the application when CI adds the bundle.
set_property(SOURCE "${CMAKE_SOURCE_DIR}/src/network/shotserver_webclient.cpp"
    APPEND PROPERTY COMPILE_DEFINITIONS DECENZA_EMBEDDED_WEBCLIENT)
