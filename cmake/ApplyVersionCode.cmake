# Read current version code (no increment — CI bumps versioncode.txt before configure)
file(READ "${VERSION_CODE_FILE}" VERSION_CODE)
string(STRIP "${VERSION_CODE}" VERSION_CODE)

# Generate version_code.cpp so the build number is compiled into the binary.
# This is a .cpp file (not a header) so Ninja/MSBuild properly detect the change
# and recompile it every build. Using BYPRODUCTS in CMakeLists.txt ensures the
# build system knows this target produces the file.
if(DEFINED VERSION_CODE_CPP AND DEFINED VERSION_CODE_CPP_TEMPLATE)
    set(CURRENT_VERSION_CODE ${VERSION_CODE})
    configure_file("${VERSION_CODE_CPP_TEMPLATE}" "${VERSION_CODE_CPP}" @ONLY)
endif()

# Update AndroidManifest.xml
if(DEFINED MANIFEST_FILE AND EXISTS "${MANIFEST_FILE}")
    file(READ "${MANIFEST_FILE}" MANIFEST_CONTENT)

    # Update versionCode (always incrementing integer)
    string(REGEX REPLACE "android:versionCode=\"[0-9]+\""
           "android:versionCode=\"${VERSION_CODE}\""
           MANIFEST_CONTENT "${MANIFEST_CONTENT}")

    # Update versionName to display version from CMake
    string(REGEX REPLACE "android:versionName=\"[^\"]+\""
           "android:versionName=\"${VERSION_STRING}\""
           MANIFEST_CONTENT "${MANIFEST_CONTENT}")

    file(WRITE "${MANIFEST_FILE}" "${MANIFEST_CONTENT}")
endif()

# Update Windows installer version
if(DEFINED ISS_TEMPLATE AND DEFINED ISS_OUTPUT)
    configure_file("${ISS_TEMPLATE}" "${ISS_OUTPUT}" @ONLY)
endif()
message(STATUS "Version: ${VERSION_STRING} (code: ${VERSION_CODE})")
