# Sets DECENZA_QT_VERSION from qt-version.txt at the repository root, the only place the
# Qt version is written. Standalone projects under tools/ include this by relative path.
include_guard(GLOBAL)

get_filename_component(_decenza_qt_version_file "${CMAKE_CURRENT_LIST_DIR}/../qt-version.txt" ABSOLUTE)
if(NOT EXISTS "${_decenza_qt_version_file}")
    message(FATAL_ERROR "Missing ${_decenza_qt_version_file}: it holds the Qt version to build against.")
endif()
file(READ "${_decenza_qt_version_file}" DECENZA_QT_VERSION)
string(STRIP "${DECENZA_QT_VERSION}" DECENZA_QT_VERSION)
if(NOT DECENZA_QT_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR
        "${_decenza_qt_version_file} must hold one Qt version as MAJOR.MINOR.PATCH; got '${DECENZA_QT_VERSION}'.")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_decenza_qt_version_file}")
