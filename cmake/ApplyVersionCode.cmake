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

# Generate AndroidManifest.xml from template
if(DEFINED MANIFEST_FILE AND DEFINED MANIFEST_TEMPLATE AND EXISTS "${MANIFEST_TEMPLATE}")
    set(CURRENT_VERSION_CODE ${VERSION_CODE})
    configure_file("${MANIFEST_TEMPLATE}" "${MANIFEST_FILE}" @ONLY)
endif()

# Update Windows installer version
if(DEFINED ISS_TEMPLATE AND DEFINED ISS_OUTPUT)
    configure_file("${ISS_TEMPLATE}" "${ISS_OUTPUT}" @ONLY)
endif()
message(STATUS "Version: ${VERSION_STRING} (code: ${VERSION_CODE})")
