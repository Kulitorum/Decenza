# Qt MQTT, compiled in-tree from its unmodified upstream source.
#
# Qt publishes Qt MQTT binaries only to commercial licensees, so neither the open-source
# installer nor install-qt-action can provide the library. Its source is GPL-3.0
# (compatible with Decenza).
#
# The tag is derived from the Qt this configure found, never written down here, so a Qt
# bump moves Qt MQTT with it. Upstream's own CMakeLists uses qt_internal_add_module(),
# which needs Qt's internal build machinery, so the library is compiled directly instead.
#
# Offline: -DFETCHCONTENT_SOURCE_DIR_QTMQTT=$HOME/Qt/<ver>/Src/qtmqtt (the installer's
# Sources component). The version guard below checks whatever tree that points at.

set(_qtmqtt_repo "https://github.com/qt/qtmqtt.git")
set(_qtmqtt_tag "v${Qt6_VERSION}")

# FetchContent's own failure for a missing tag is a git error buried in a sub-build log.
# Check the tag first, but only when a fetch is actually about to happen.
set(_qtmqtt_existing "${FETCHCONTENT_BASE_DIR}/qtmqtt-src/.cmake.conf")
if(NOT FETCHCONTENT_SOURCE_DIR_QTMQTT AND NOT FETCHCONTENT_FULLY_DISCONNECTED)
    set(_qtmqtt_need_check TRUE)
    if(EXISTS "${_qtmqtt_existing}")
        file(STRINGS "${_qtmqtt_existing}" _qtmqtt_line REGEX "QT_REPO_MODULE_VERSION \"")
        if(_qtmqtt_line MATCHES "\"${Qt6_VERSION}\"")
            set(_qtmqtt_need_check FALSE)
        endif()
    endif()
    if(_qtmqtt_need_check)
        find_package(Git REQUIRED)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" ls-remote --exit-code --tags "${_qtmqtt_repo}" "refs/tags/${_qtmqtt_tag}"
            RESULT_VARIABLE _qtmqtt_rc OUTPUT_QUIET ERROR_VARIABLE _qtmqtt_err)
        if(NOT _qtmqtt_rc EQUAL 0)
            message(FATAL_ERROR
                "Qt MQTT: no release tag ${_qtmqtt_tag} in ${_qtmqtt_repo} for Qt ${Qt6_VERSION} "
                "(git ls-remote exit ${_qtmqtt_rc}; 2 = tag missing, other = network). "
                "Qt MQTT is built from source at the tag matching the Qt version.\n${_qtmqtt_err}")
        endif()
    endif()
endif()

include(FetchContent)
FetchContent_Declare(
    qtmqtt
    GIT_REPOSITORY ${_qtmqtt_repo}
    GIT_TAG        ${_qtmqtt_tag}
    GIT_SHALLOW    TRUE
    # A directory without a CMakeLists.txt: populate the source, don't run upstream's build.
    SOURCE_SUBDIR  _decenza_no_cmake
)
FetchContent_MakeAvailable(qtmqtt)

# Only the version field: upstream's prerelease segment still reads "alpha1" on the
# v6.12.0 release tag (.cmake.conf line 2), so it cannot be part of the comparison.
file(STRINGS "${qtmqtt_SOURCE_DIR}/.cmake.conf" _qtmqtt_line REGEX "QT_REPO_MODULE_VERSION \"")
string(REGEX MATCH "\"([^\"]+)\"" _qtmqtt_match "${_qtmqtt_line}")
set(_qtmqtt_version "${CMAKE_MATCH_1}")
if(NOT _qtmqtt_version STREQUAL Qt6_VERSION)
    message(FATAL_ERROR
        "Qt MQTT source at ${qtmqtt_SOURCE_DIR} is version '${_qtmqtt_version}', "
        "but this build uses Qt ${Qt6_VERSION}. Qt MQTT uses Qt's private headers and must "
        "match exactly. Delete that directory, or point FETCHCONTENT_SOURCE_DIR_QTMQTT at "
        "a ${Qt6_VERSION} tree.")
endif()
message(STATUS "Qt MQTT ${_qtmqtt_version} (source: ${qtmqtt_SOURCE_DIR})")

set(_qtmqtt_src "${qtmqtt_SOURCE_DIR}/src/mqtt")
set(_qtmqtt_sources
    qmqttauthenticationproperties.cpp
    qmqttclient.cpp
    qmqttconnection.cpp
    qmqttconnectionproperties.cpp
    qmqttcontrolpacket.cpp
    qmqttmessage.cpp
    qmqttpublishproperties.cpp
    qmqttsubscription.cpp
    qmqttsubscriptionproperties.cpp
    qmqtttopicfilter.cpp
    qmqtttopicname.cpp
    qmqtttype.cpp
)
file(GLOB _qtmqtt_headers RELATIVE "${_qtmqtt_src}" "${_qtmqtt_src}/*.h")

# <QtMqtt/x.h> forwarding headers point at the one real copy, so a class is never
# defined from two files. qtmqttexports.h is the one generated header these sources include.
set(_qtmqtt_inc "${CMAKE_BINARY_DIR}/qtmqtt_include")
foreach(_h IN LISTS _qtmqtt_headers)
    file(CONFIGURE OUTPUT "${_qtmqtt_inc}/QtMqtt/${_h}"
         CONTENT "#include \"${_qtmqtt_src}/${_h}\"\n")
endforeach()
file(CONFIGURE OUTPUT "${_qtmqtt_inc}/QtMqtt/qtmqttexports.h" CONTENT [=[
#ifndef QTMQTTEXPORTS_H
#define QTMQTTEXPORTS_H
// Static, in-tree build (cmake/qtmqtt.cmake): nothing is exported or imported.
#define Q_MQTT_EXPORT
#endif
]=])

list(TRANSFORM _qtmqtt_sources PREPEND "${_qtmqtt_src}/")
list(TRANSFORM _qtmqtt_headers PREPEND "${_qtmqtt_src}/")
add_library(decenza_qtmqtt STATIC ${_qtmqtt_sources} ${_qtmqtt_headers})
set_target_properties(decenza_qtmqtt PROPERTIES AUTOMOC ON)
target_include_directories(decenza_qtmqtt
    PUBLIC  "${_qtmqtt_inc}"
    PRIVATE "${_qtmqtt_src}")
target_compile_definitions(decenza_qtmqtt PRIVATE
    # From upstream's .cmake.conf (QT_EXTRA_INTERNAL_TARGET_DEFINES).
    QT_NO_QASCONST=1 QT_NO_FOREACH=1 QT_NO_SINGLE_ARGUMENT_QHASH_OVERLOAD=1)
target_link_libraries(decenza_qtmqtt
    PUBLIC  Qt6::Core Qt6::Network
    PRIVATE Qt6::CorePrivate)
# Third-party source: don't hold it to Decenza's own warning flags.
if(MSVC)
    target_compile_options(decenza_qtmqtt PRIVATE /W0)
else()
    target_compile_options(decenza_qtmqtt PRIVATE -w)
endif()
