# Read current version code
file(READ "${VERSION_CODE_FILE}" VERSION_CODE)
string(STRIP "${VERSION_CODE}" VERSION_CODE)

# Increment
math(EXPR VERSION_CODE "${VERSION_CODE} + 1")

# Write back
file(WRITE "${VERSION_CODE_FILE}" "${VERSION_CODE}\n")

# Regenerate version.h so VERSION_CODE stays in sync with manifest
if(DEFINED VERSION_HEADER AND DEFINED VERSION_TEMPLATE)
    set(NEXT_VERSION_CODE ${VERSION_CODE})
    configure_file("${VERSION_TEMPLATE}" "${VERSION_HEADER}" @ONLY)
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
