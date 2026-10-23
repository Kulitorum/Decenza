# Keep the generated replica in AUTOMOC so its metadata reaches qmltyperegistrar.
# Qt 6.12 Qt6RemoteObjectsMacros.cmake:79-88 uses manual moc without JSON for
# qt6_add_repc_replicas(), which leaves a QML_FOREIGN replica's properties unknown.
# Generate the same stock repc header, then let the QML target collect its moc JSON.
function(decenza_add_remote_object_replica target rep_name)
    set(_rep "${CMAKE_SOURCE_DIR}/src/remote/${rep_name}.rep")
    set(_header "${CMAKE_CURRENT_BINARY_DIR}/rep_${rep_name}_replica.h")
    add_custom_command(OUTPUT "${_header}"
        COMMAND Qt6::repc -o replica "${_rep}" "${_header}"
        MAIN_DEPENDENCY "${_rep}"
        DEPENDS Qt6::repc
        VERBATIM)
    set_source_files_properties("${_header}" PROPERTIES GENERATED TRUE SKIP_AUTOGEN FALSE)
    target_sources(${target} PRIVATE "${_header}")
    get_target_property(_license_added ${target} DECENZA_QTRO_LICENSE_ADDED)
    if(NOT _license_added)
        qt_add_resources(${target} "decenza_remote_licenses"
            PREFIX "/licenses/qtro-websocket"
            BASE "${CMAKE_SOURCE_DIR}/src/remote"
            FILES "${CMAKE_SOURCE_DIR}/src/remote/LICENSE-BSD-3-Clause.txt")
        set_property(TARGET ${target} PROPERTY DECENZA_QTRO_LICENSE_ADDED TRUE)
    endif()
endfunction()
