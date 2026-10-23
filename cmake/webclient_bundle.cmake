# Native release packages carry the verified client in the Qt resource system,
# so Android/iOS and desktop installers use the same bundle and HTTP reader.
set(DECENZA_WEB_BUNDLE_DIR "" CACHE PATH "Matched Wasm deployment directory to package with the native app")
option(DECENZA_REQUIRE_WEB_BUNDLE "Fail configuration without a matching Wasm deployment (release CI)" OFF)
option(DECENZA_EMBED_WEB_BUNDLE "Embed the verified bundle (OFF retains local macOS resource-directory packaging)" ON)

if(NOT DECENZA_WEB_BUNDLE_DIR)
    if(DECENZA_REQUIRE_WEB_BUNDLE)
        message(FATAL_ERROR "Release packaging requires DECENZA_WEB_BUNDLE_DIR")
    endif()
    return()
endif()

if(NOT DECENZA_EMBED_WEB_BUNDLE)
    if(NOT APPLE OR IOS OR DECENZA_REQUIRE_WEB_BUNDLE)
        message(FATAL_ERROR "External web client packaging is only for local macOS builds")
    endif()
    return()
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(_webclient_staged "${CMAKE_BINARY_DIR}/webclient-bundle")
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/package_webclient.py"
        install --source "${DECENZA_WEB_BUNDLE_DIR}"
        --expected-package-id "${DECENZA_PACKAGE_ID}"
        --output-dir "${_webclient_staged}"
    RESULT_VARIABLE _webclient_result
    OUTPUT_VARIABLE _webclient_output
    ERROR_VARIABLE _webclient_error)
if(NOT _webclient_result EQUAL 0)
    message(FATAL_ERROR "Matching web client verification failed: ${_webclient_error}")
endif()
message(STATUS "${_webclient_output}")

file(GLOB _webclient_inputs CONFIGURE_DEPENDS "${DECENZA_WEB_BUNDLE_DIR}/*")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_webclient_inputs})
file(GLOB _webclient_files "${_webclient_staged}/*")

# Qt 6.12 qt_add_resources documents BIG_RESOURCES as incompatible with iOS's
# Xcode generator. Use direct object generation elsewhere; compress iOS's C++
# resource representation so it does not expand the full Wasm into source.
# https://doc.qt.io/qt-6.12/qt-add-resources.html#arguments-of-the-target-based-variant
# Source: qtbase/src/corelib/Qt6CoreMacros.cmake:2489-2496.
set(_webclient_big_resources)
if(NOT IOS)
    set(_webclient_big_resources BIG_RESOURCES)
endif()
qt_add_resources(Decenza packaged_webclient
    ${_webclient_big_resources}
    PREFIX "/webclient" BASE "${_webclient_staged}"
    FILES ${_webclient_files}
    OPTIONS --compress-algo zlib --compress 9)
target_compile_definitions(Decenza PRIVATE DECENZA_EMBEDDED_WEBCLIENT)
