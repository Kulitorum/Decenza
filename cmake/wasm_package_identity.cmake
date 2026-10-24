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

    set(_identity_material "Decenza source package identity v1\n")
    foreach(_file IN LISTS _identity_inputs)
        if(NOT EXISTS "${_file}" OR IS_DIRECTORY "${_file}")
            message(FATAL_ERROR "Package identity input is missing or not a file: ${_file}")
        endif()
        file(RELATIVE_PATH _relative "${_source_root}" "${_file}")
        string(REPLACE "\\" "/" _relative "${_relative}")
        file(SHA256 "${_file}" _file_hash)
        string(APPEND _identity_material "${_relative}\n${_file_hash}\n")
        # CONFIGURE_DEPENDS notices additions/removals. This explicit property
        # also reruns CMake when an existing source or resource's contents
        # change, refreshing the header before the next compile.
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_file}")
    endforeach()

    string(SHA256 _package_id "${_identity_material}")
    set(_header_directory "${_output_directory}/generated")
    set(_header "${_header_directory}/decenza_package_identity.h")
    file(MAKE_DIRECTORY "${_header_directory}")
    set(_header_contents
        "#pragma once\n#define DECENZA_PACKAGE_ID \"${_package_id}\"\n")
    if(NOT EXISTS "${_header}")
        file(WRITE "${_header}" "${_header_contents}")
    else()
        file(READ "${_header}" _existing_header)
        if(NOT "${_existing_header}" STREQUAL "${_header_contents}")
            file(WRITE "${_header}" "${_header_contents}")
        endif()
    endif()

    set(DECENZA_PACKAGE_ID "${_package_id}" PARENT_SCOPE)
    set(DECENZA_PACKAGE_ID_HEADER "${_header}" PARENT_SCOPE)
    set(DECENZA_PACKAGE_ID_INCLUDE_DIR "${_header_directory}" PARENT_SCOPE)
endfunction()
