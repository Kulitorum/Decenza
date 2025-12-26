# Read current build number
file(READ "${BUILD_NUMBER_FILE}" BUILD_NUMBER)
string(STRIP "${BUILD_NUMBER}" BUILD_NUMBER)

set(AAB_SOURCE "${BUILD_DIR}/android-build-Decenza_DE1/build/outputs/bundle/release/android-build-Decenza_DE1-release.aab")
set(AAB_DEST "${SOURCE_DIR}/Decenza_DE1-1.0.${BUILD_NUMBER}.aab")

if(EXISTS "${AAB_SOURCE}")
    file(COPY_FILE "${AAB_SOURCE}" "${AAB_DEST}")
    message(STATUS "Created: ${AAB_DEST}")
else()
    message(FATAL_ERROR "AAB not found at: ${AAB_SOURCE}\nBuild the AAB first with: cmake --build . --target aab")
endif()
