# Point a QML module at qmlcachegen_stable.py, which leaves byte-identical
# outputs untouched so Ninja's restat stops recompiling unchanged QML units.
# Call before qt_add_qml_module(<target>): Qt reads the property there.
# Ninja only: Xcode, Visual Studio and Makefiles have no restat, so an output left
# older than its inputs would re-run its cachegen on every build.
#
# QT_QMLCACHEGEN_EXECUTABLE is Qt's documented override
# (qt_add_qml_module.qdoc:270-278). A value other than qmlcachegen/qmlsc goes
# through find_program (Qt6QmlMacros.cmake:3677-3690 in 6.12.0), which needs an
# executable file, hence the generated launcher.
function(decenza_use_stable_qmlcachegen target)
    if(NOT CMAKE_GENERATOR MATCHES "Ninja")
        return()
    endif()
    if(TARGET Qt6::qmlsc)
        set(_real_tool Qt6::qmlsc)
    else()
        set(_real_tool Qt6::qmlcachegen)
    endif()
    get_target_property(_real "${_real_tool}" LOCATION)
    if(NOT _real OR NOT EXISTS "${_real}")
        message(FATAL_ERROR "Cannot locate ${_real_tool} for the stable qmlcachegen wrapper")
    endif()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)

    set(_wrapper "${CMAKE_SOURCE_DIR}/cmake/qmlcachegen_stable.py")
    set(_dir "${CMAKE_BINARY_DIR}/qmlcachegen_stable")
    if(CMAKE_HOST_WIN32)
        set(_launcher "${_dir}/qmlcachegen_stable.cmd")
        set(_content "@\"${Python3_EXECUTABLE}\" -S \"${_wrapper}\" \"${_real}\" %*\r\n")
    else()
        set(_launcher "${_dir}/qmlcachegen_stable")
        set(_content "#!/bin/sh\nexec \"${Python3_EXECUTABLE}\" -S \"${_wrapper}\" \"${_real}\" \"$@\"\n")
    endif()
    file(CONFIGURE OUTPUT "${_launcher}" CONTENT "${_content}" @ONLY)
    file(CHMOD "${_launcher}" PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

    # Cachegen need not depend on the .py: the wrapper only decides whether an
    # identical output is replaced, never what an output contains. Qt's commands
    # now depend on the launcher instead of the compiler, so the wiring below adds
    # the compiler back.
    set_target_properties(${target} PROPERTIES QT_QMLCACHEGEN_EXECUTABLE "${_launcher}"
        _DECENZA_REAL_QMLCACHEGEN "${_real}")
endfunction()

# The unit path Qt's cachegen writes for <abs> in <target>, from the current
# directory scope. Qt's helper where it exists (Qt6QmlMacros.cmake:2548 in the 6.12.0
# macOS kit). The iOS 6.12.0 package that CI installs (qt.qt6.6120.ios,
# 6.12.0-0-202607081031) predates it and computes the path inline
# (Qt6QmlMacros.cmake:3876-3904), with the same rule copied here.
function(_decenza_qml_cachegen_unit target abs out_var)
    if(COMMAND _qt_internal_qml_get_cachegen_compiled_file)
        _qt_internal_qml_get_cachegen_compiled_file(TARGET ${target}
            SOURCE_QML_FILE_PATH "${abs}" OUT_VAR_GENERATED_CPP_FILE_PATH _unit)
        set(${out_var} "${_unit}" PARENT_SCOPE)
        return()
    endif()
    file(RELATIVE_PATH _from_source "${CMAKE_CURRENT_SOURCE_DIR}" "${abs}")
    file(RELATIVE_PATH _from_binary "${CMAKE_CURRENT_BINARY_DIR}" "${abs}")
    string(LENGTH "${_from_source}" _source_length)
    string(LENGTH "${_from_binary}" _binary_length)
    if(_source_length LESS _binary_length)
        set(_relative "${_from_source}")
    else()
        set(_relative "${_from_binary}")
    endif()
    string(REGEX REPLACE "\\.\\.[/\\\\]" "_" _name "${_relative}")
    string(REGEX REPLACE "\\.(js|mjs|qml)$" "_\\1" _name "${_name}")
    string(REGEX REPLACE "[$#?/ ]+" "_" _name "${_name}")
    set(${out_var} "${CMAKE_CURRENT_BINARY_DIR}/.rcc/qmlcache/${target}_${_name}.cpp" PARENT_SCOPE)
endfunction()

# Make every compiled unit of a QML module depend on every QML/JS source of that
# module. qmlcachegen reads other files to resolve QML-declared types (Theme, every
# component used by name) but declares none of them as inputs and has no depfile, so
# Qt's own commands list only the unit's own source. Without this, editing Theme.qml
# leaves every caller's unit stale. With the stable wrapper the cost is re-running
# cachegen, not recompiling. Call after qt_add_qml_module(<target>), in the same
# directory: Qt's output-path helper resolves against the current directory.
# Sets <out_count> to the number of units wired.
function(decenza_wire_qml_cache_deps target out_count)
    get_target_property(_files ${target} QT_QML_MODULE_QML_FILES)
    set(_sources "")
    foreach(_file IN LISTS _files)
        get_filename_component(_abs "${_file}" ABSOLUTE)
        get_source_file_property(_skip "${_abs}" QT_QML_SKIP_CACHEGEN)
        if(_abs MATCHES "\\.(qml|js|mjs)$" AND NOT _skip)
            list(APPEND _sources "${_abs}")
        endif()
    endforeach()
    get_target_property(_compiler ${target} _DECENZA_REAL_QMLCACHEGEN)
    if(NOT _compiler)
        set(_compiler "")
    endif()
    # An APPEND to an OUTPUT Qt did not create is a configure error, so a changed
    # output path fails loudly rather than detaching.
    foreach(_abs IN LISTS _sources)
        _decenza_qml_cachegen_unit(${target} "${_abs}" _unit)
        add_custom_command(OUTPUT "${_unit}" APPEND DEPENDS ${_sources} ${_compiler})
    endforeach()
    list(LENGTH _sources _count)
    message(STATUS "QML cross-file cache deps wired for ${_count} ${target} units")
    set(${out_count} ${_count} PARENT_SCOPE)
endfunction()
