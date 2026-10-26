# Produce one source-tree identity for native and Wasm builds. It deliberately
# does not include the build directory, generator, compiler, Qt kit, or Git
# metadata: separate kits built from the same working tree must agree, including
# edits and new untracked source files.
function(decenza_generate_package_identity source_root output_directory)
    get_filename_component(_source_root "${source_root}" REALPATH)
    get_filename_component(_output_directory "${output_directory}" ABSOLUTE)

    set(_identity_inputs
        "${_source_root}/CMakeLists.txt"
        "${_source_root}/build.sh"
        "${_source_root}/versioncode.txt"
    )

    # These are the code/resource trees used by the native app and optional
    # web client. Explicit roots avoid accidentally pulling in tests,
    # documentation, OpenSpec evidence, generated build trees, or local Qt
    # Creator metadata.
    file(GLOB_RECURSE _identity_tree_files CONFIGURE_DEPENDS LIST_DIRECTORIES false
        "${_source_root}/src/*"
        "${_source_root}/qml/*"
        "${_source_root}/resources/*"
        "${_source_root}/shaders/*"
        "${_source_root}/cmake/*"
        "${_source_root}/scripts/*"
    )
    # Diagnostic QML and recorded-trace sources are compiled into the client.
    # Keep this shallow: the directory also retains local build data.
    file(GLOB _webclient_tool_files CONFIGURE_DEPENDS LIST_DIRECTORIES false
        "${_source_root}/tools/webclient/*.cpp"
        "${_source_root}/tools/webclient/*.h"
        "${_source_root}/tools/webclient/*.qml"
        "${_source_root}/tools/webclient/*.js"
        "${_source_root}/tools/webclient/*.html"
    )
    list(APPEND _identity_tree_files ${_webclient_tool_files})
    # Hash only source/runtime/resource and build-recipe formats. This avoids
    # editor backups, bytecode caches, markdown evidence, and machine-local
    # metadata influencing an otherwise identical pair of kits.
    set(_identity_extensions
        .rep .cpp .h .hpp .mm .m .qml .js .mjs .html .css .qrc .in .cmake .py .sh
        .svg .ttf .wav .png .jpg .jpeg .ico .json .bin .7z .frag .vert .glsl .txt
    )

    foreach(_file IN LISTS _identity_tree_files)
        file(RELATIVE_PATH _relative "${_source_root}" "${_file}")
        string(REPLACE "\\" "/" _relative "${_relative}")
        get_filename_component(_extension "${_relative}" EXT)
        string(TOLOWER "${_extension}" _extension)
        if((NOT _extension IN_LIST _identity_extensions AND NOT _relative MATCHES "^resources/")
                OR _relative MATCHES "(^|/)\\.[^/]+(/|$)"
                OR _relative MATCHES "(^|/)__pycache__(/|$)"
                OR _relative STREQUAL "resources/GHC.psd"
                OR _relative MATCHES "(^|/)(build|out|\\.qtcreator|tests|docs|evidence)(/|$)"
                OR _relative MATCHES "\\.(pyc|user|bak|swp)$"
                OR _relative MATCHES "^tools/webclient/(README[^/]*|upstream-accessibility\\.md|test_[^/]+)$")
            continue()
        endif()

        # A package's human-facing version and embedded version code are both
        # identity inputs; the generated version.h/version_code.cpp live in the
        # per-kit build directory and are intentionally absent here.
        list(APPEND _identity_inputs "${_file}")
    endforeach()

    list(REMOVE_DUPLICATES _identity_inputs)
    list(SORT _identity_inputs)

    set(_header_directory "${_output_directory}/generated")
    set(_header "${_header_directory}/decenza_package_identity.h")
    set(_input_list "${_header_directory}/package_identity_inputs.txt")
    file(MAKE_DIRECTORY "${_header_directory}")
    string(JOIN "\n" _input_list_contents ${_identity_inputs})
    _decenza_write_if_changed("${_input_list}" "${_input_list_contents}\n")

    # Hash at build time. Hashing at configure time made every input a
    # CMAKE_CONFIGURE_DEPENDS, so any source edit re-ran CMake in both the native
    # and the Wasm build. The CONFIGURE_DEPENDS globs above still reconfigure when
    # a file is added or removed.
    add_custom_command(OUTPUT "${_header}"
        COMMAND "${CMAKE_COMMAND}" "-DSOURCE_ROOT=${_source_root}"
            "-DINPUT_LIST=${_input_list}" "-DHEADER=${_header}"
            -P "${CMAKE_CURRENT_FUNCTION_LIST_FILE}"
        DEPENDS ${_identity_inputs} "${_input_list}" "${CMAKE_CURRENT_FUNCTION_LIST_FILE}"
        COMMENT "Hashing the source package identity"
        VERBATIM)
    add_custom_target(decenza_package_identity_header DEPENDS "${_header}")

    set(DECENZA_PACKAGE_ID_HEADER "${_header}" PARENT_SCOPE)
    set(DECENZA_PACKAGE_ID_INCLUDE_DIR "${_header_directory}" PARENT_SCOPE)
    set_property(GLOBAL PROPERTY _decenza_package_identity_args
        "${_source_root}" "${_input_list}" "${_header}")
endfunction()

# The id as a configure-time value, for steps that verify at configure time
# (the PREBUILT/release path). Writes the header too, so it matches the build step.
function(decenza_package_identity_now out_var)
    get_property(_args GLOBAL PROPERTY _decenza_package_identity_args)
    _decenza_write_package_identity(${_args} _id)
    set(${out_var} "${_id}" PARENT_SCOPE)
endfunction()

function(_decenza_write_package_identity source_root input_list header out_var)
    file(STRINGS "${input_list}" _inputs)
    set(_identity_material "Decenza source package identity v1\n")
    foreach(_file IN LISTS _inputs)
        if(NOT EXISTS "${_file}" OR IS_DIRECTORY "${_file}")
            message(FATAL_ERROR "Package identity input is missing or not a file: ${_file}")
        endif()
        file(RELATIVE_PATH _relative "${source_root}" "${_file}")
        string(REPLACE "\\" "/" _relative "${_relative}")
        file(SHA256 "${_file}" _file_hash)
        string(APPEND _identity_material "${_relative}\n${_file_hash}\n")
    endforeach()
    string(SHA256 _package_id "${_identity_material}")
    _decenza_write_if_changed("${header}"
        "#pragma once\n#define DECENZA_PACKAGE_ID \"${_package_id}\"\n")
    set(${out_var} "${_package_id}" PARENT_SCOPE)
endfunction()

# Leaves an unchanged file's timestamp alone, so restat prunes its consumers.
function(_decenza_write_if_changed path contents)
    if(EXISTS "${path}")
        file(READ "${path}" _existing)
        if(_existing STREQUAL contents)
            return()
        endif()
    endif()
    file(WRITE "${path}" "${contents}")
endfunction()

function(decenza_add_package_identity_library)
    add_library(decenza_package_identity STATIC
        "${CMAKE_SOURCE_DIR}/src/core/packageidentity.cpp")
    set_target_properties(decenza_package_identity PROPERTIES
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    add_dependencies(decenza_package_identity decenza_package_identity_header)
endfunction()

if(CMAKE_SCRIPT_MODE_FILE STREQUAL CMAKE_CURRENT_LIST_FILE)
    _decenza_write_package_identity("${SOURCE_ROOT}" "${INPUT_LIST}" "${HEADER}" _id)
endif()
