cmake_minimum_required(VERSION 3.21)

project(DecenzaQtMultimediaWasm LANGUAGES C CXX)

include(ExternalProject)

find_package(Qt6 6.12.0 EXACT REQUIRED COMPONENTS Core)

set(QT_MULTIMEDIA_SOURCE_DIR "" CACHE PATH
    "Path to the unmodified Qt 6.12.0 Src/qtmultimedia checkout (auto-detected beside the selected Qt kit)")
set(QT_MULTIMEDIA_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/../../build/qtmultimedia-wasm-install"
    CACHE PATH "Workspace-local install prefix for the Wasm Qt Multimedia module")

if(NOT QT_MULTIMEDIA_SOURCE_DIR)
    get_filename_component(_qt_sdk_root "${Qt6_DIR}/../../../../" ABSOLUTE)
    set(_qt_multimedia_source_candidate "${_qt_sdk_root}/Src/qtmultimedia")
    if(EXISTS "${_qt_multimedia_source_candidate}/CMakeLists.txt"
       AND EXISTS "${_qt_multimedia_source_candidate}/.cmake.conf")
        set(QT_MULTIMEDIA_SOURCE_DIR "${_qt_multimedia_source_candidate}" CACHE PATH
            "Path to the unmodified Qt 6.12.0 Src/qtmultimedia checkout (auto-detected beside the selected Qt kit)"
            FORCE)
    endif()
endif()

if(NOT QT_MULTIMEDIA_SOURCE_DIR OR
   NOT EXISTS "${QT_MULTIMEDIA_SOURCE_DIR}/CMakeLists.txt" OR
   NOT EXISTS "${QT_MULTIMEDIA_SOURCE_DIR}/.cmake.conf")
    message(FATAL_ERROR
        "Could not find Qt Multimedia 6.12.0 sources beside the selected kit. Set QT_MULTIMEDIA_SOURCE_DIR to its Src/qtmultimedia directory.")
endif()

file(STRINGS "${QT_MULTIMEDIA_SOURCE_DIR}/.cmake.conf" _qt_multimedia_version_line
    REGEX "^set\\(QT_REPO_MODULE_VERSION ")
if(NOT _qt_multimedia_version_line MATCHES
   "set\\(QT_REPO_MODULE_VERSION \"6\\.12\\.0\"\\)")
    message(FATAL_ERROR
        "Expected Qt Multimedia source version 6.12.0 in ${QT_MULTIMEDIA_SOURCE_DIR}/.cmake.conf; got '${_qt_multimedia_version_line}'.")
endif()

if(NOT CMAKE_TOOLCHAIN_FILE OR NOT EXISTS "${CMAKE_TOOLCHAIN_FILE}")
    message(FATAL_ERROR "Configure this project with the Qt 6.12.0 wasm_singlethread kit.")
endif()
if(NOT QT_HOST_PATH OR NOT IS_DIRECTORY "${QT_HOST_PATH}")
    message(FATAL_ERROR "The Wasm kit must provide QT_HOST_PATH for native Qt build tools.")
endif()
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    message(FATAL_ERROR "This module build project is only for the Qt 6.12.0 WebAssembly kit.")
endif()

if(NOT TARGET Qt6::Platform)
    message(FATAL_ERROR "The selected Qt package has no Qt6::Platform target.")
endif()
get_target_property(_qt_platform_link_options Qt6::Platform INTERFACE_LINK_OPTIONS)
string(JOIN " " _qt_platform_link_options_text ${_qt_platform_link_options})
if(_qt_platform_link_options_text MATCHES "-pthread|PTHREADS|SHARED_MEMORY")
    message(FATAL_ERROR "Use the wasm_singlethread kit; this build must not enable Wasm threads.")
endif()

set(QT_MULTIMEDIA_BUILD_TYPE "Release" CACHE STRING "Build type of the packaged Wasm module")

set(_qt_multimedia_binary_dir "${CMAKE_BINARY_DIR}/qtmultimedia")
string(REPLACE ";" "|" _qt_multimedia_prefix_path "${CMAKE_PREFIX_PATH}")

ExternalProject_Add(qtmultimedia
    SOURCE_DIR "${QT_MULTIMEDIA_SOURCE_DIR}"
    BINARY_DIR "${_qt_multimedia_binary_dir}"
    DOWNLOAD_COMMAND ""
    UPDATE_COMMAND ""
    CMAKE_GENERATOR "${CMAKE_GENERATOR}"
    LIST_SEPARATOR "|"
    CMAKE_ARGS
        "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${CMAKE_TOOLCHAIN_FILE}"
        "-DQT_HOST_PATH:PATH=${QT_HOST_PATH}"
        "-DQt6_DIR:PATH=${Qt6_DIR}"
        "-DCMAKE_PREFIX_PATH:PATH=${_qt_multimedia_prefix_path}"
        "-DCMAKE_BUILD_TYPE:STRING=${QT_MULTIMEDIA_BUILD_TYPE}"
        "-DCMAKE_INSTALL_PREFIX:PATH=${QT_MULTIMEDIA_INSTALL_PREFIX}"
        "-DCMAKE_STAGING_PREFIX:PATH=${QT_MULTIMEDIA_INSTALL_PREFIX}"
        "-DQT_BUILD_TESTS:BOOL=OFF"
        "-DQT_BUILD_EXAMPLES:BOOL=OFF"
        "-DQT_BUILD_DOCS:BOOL=OFF"
    BUILD_ALWAYS TRUE
    BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --parallel
    INSTALL_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --target install --parallel
    USES_TERMINAL_CONFIGURE TRUE
    USES_TERMINAL_BUILD TRUE
    USES_TERMINAL_INSTALL TRUE
    STEP_TARGETS configure build install
    COMMENT "Build and install stock Qt Multimedia 6.12.0 for wasm_singlethread"
)
