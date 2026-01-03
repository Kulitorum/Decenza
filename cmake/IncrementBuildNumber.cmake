# Read current build number
file(READ "${BUILD_NUMBER_FILE}" BUILD_NUMBER)
string(STRIP "${BUILD_NUMBER}" BUILD_NUMBER)

# Increment
math(EXPR BUILD_NUMBER "${BUILD_NUMBER} + 1")

# Write back
file(WRITE "${BUILD_NUMBER_FILE}" "${BUILD_NUMBER}\n")

# Generate header
file(WRITE "${OUTPUT_HEADER}"
"#pragma once
// Auto-generated - do not edit
#define BUILD_NUMBER ${BUILD_NUMBER}
#define BUILD_NUMBER_STRING \"${BUILD_NUMBER}\"
")

# Update AndroidManifest.xml versionCode (required for Google Play Store uploads)
# MANIFEST_FILE is passed from CMakeLists.txt
if(DEFINED MANIFEST_FILE AND EXISTS "${MANIFEST_FILE}")
    file(READ "${MANIFEST_FILE}" MANIFEST_CONTENT)
    # Replace versionCode="X" with versionCode="BUILD_NUMBER"
    string(REGEX REPLACE "android:versionCode=\"[0-9]+\""
           "android:versionCode=\"${BUILD_NUMBER}\""
           MANIFEST_CONTENT "${MANIFEST_CONTENT}")
    # Update versionName to 1.1.BUILD_NUMBER
    string(REGEX REPLACE "android:versionName=\"[^\"]+\""
           "android:versionName=\"1.1.${BUILD_NUMBER}\""
           MANIFEST_CONTENT "${MANIFEST_CONTENT}")
    file(WRITE "${MANIFEST_FILE}" "${MANIFEST_CONTENT}")
endif()

# Create git tag for this build (allows finding exact commit by build number)
# Use git tag -f to overwrite if tag exists (in case of rebuild)
find_program(GIT_EXECUTABLE git)
if(GIT_EXECUTABLE)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} tag -f "build-${BUILD_NUMBER}"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_QUIET
        ERROR_QUIET
    )
endif()

message(STATUS "Build number: ${BUILD_NUMBER}")
