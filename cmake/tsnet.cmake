# Pulls in prebuilt libtailscale artifacts from the Decenza fork
# (skialpine/libtailscale) so Mode A of the Remote MCP connector (embedded
# Tailscale + Funnel) can be built WITHOUT a Go toolchain. Enabled by
# -DENABLE_TSNET=ON; a no-op otherwise.
#
# Exposes:
#   TSNET_INCLUDE_DIR         directory containing tailscale.h
#   decenza_link_tsnet(<tgt>) links the prebuilt library + platform frameworks
#
# The pinned release + per-artifact SHA-256 come from the release manifest.json.
# Bump TSNET_TAG (and the hashes) to adopt a newer libtailscale build.

set(TSNET_TAG "decenza-v1.94.1-1" CACHE STRING "libtailscale prebuilt release tag")
set(TSNET_BASE_URL "https://github.com/skialpine/libtailscale/releases/download/${TSNET_TAG}")
set(TSNET_DOWNLOAD_DIR "${CMAKE_BINARY_DIR}/tsnet-${TSNET_TAG}")

# Per-platform artifact + expected SHA-256 (from manifest.json).
if(IOS)
    set(_tsnet_zip "libtailscale-ios.zip")
    set(_tsnet_sha "3edb45778be4117c8a034bae43da8f5b566c50f43596c197d0c4da983abdc427")
elseif(ANDROID)
    set(_tsnet_zip "libtailscale-android.zip")
    set(_tsnet_sha "e43497b13de23ccabd90487a9bb2c2d8dbc7f3d6d492ebfdbbf0965016505f4c")
elseif(APPLE)
    set(_tsnet_zip "libtailscale-macos.zip")
    set(_tsnet_sha "b2a9041cb271af56dea6d9050927beb335d4eeff7427d44632c31c206f283f78")
else()
    message(FATAL_ERROR "ENABLE_TSNET: no prebuilt libtailscale artifact for this platform yet "
                        "(supported: macOS, Android, iOS). Use Mode C (BYO URL) instead.")
endif()

set(_tsnet_archive "${TSNET_DOWNLOAD_DIR}/${_tsnet_zip}")
set(_tsnet_extract "${TSNET_DOWNLOAD_DIR}/extracted")

if(NOT EXISTS "${_tsnet_extract}/.stamp")
    message(STATUS "tsnet: downloading ${_tsnet_zip} from ${TSNET_TAG}")
    file(DOWNLOAD "${TSNET_BASE_URL}/${_tsnet_zip}" "${_tsnet_archive}"
         EXPECTED_HASH "SHA256=${_tsnet_sha}"
         STATUS _dl_status
         SHOW_PROGRESS)
    list(GET _dl_status 0 _dl_code)
    if(NOT _dl_code EQUAL 0)
        list(GET _dl_status 1 _dl_msg)
        message(FATAL_ERROR "tsnet: download of ${_tsnet_zip} failed: ${_dl_msg}")
    endif()
    file(REMOVE_RECURSE "${_tsnet_extract}")
    file(MAKE_DIRECTORY "${_tsnet_extract}")
    file(ARCHIVE_EXTRACT INPUT "${_tsnet_archive}" DESTINATION "${_tsnet_extract}")
    file(TOUCH "${_tsnet_extract}/.stamp")
endif()

# Resolve the include dir + platform-specific link target.
if(APPLE AND NOT IOS)
    set(TSNET_INCLUDE_DIR "${_tsnet_extract}/libtailscale-macos/include" CACHE PATH "" FORCE)
    set(_tsnet_lib "${_tsnet_extract}/libtailscale-macos/libtailscale.a")
    if(NOT EXISTS "${_tsnet_lib}")
        message(FATAL_ERROR "tsnet: libtailscale.a missing after extract "
                            "(delete ${TSNET_DOWNLOAD_DIR} to re-download): ${_tsnet_lib}")
    endif()
    function(decenza_link_tsnet tgt)
        target_include_directories(${tgt} PRIVATE "${TSNET_INCLUDE_DIR}")
        # The Go static archive needs the darwin system frameworks tsnet uses.
        target_link_libraries(${tgt} PRIVATE
            "${_tsnet_lib}"
            "-framework CoreFoundation"
            "-framework Security"
            "-framework SystemConfiguration"
            resolv)
    endfunction()

elseif(IOS)
    set(_tsnet_xcf "${_tsnet_extract}/libtailscale.xcframework")
    # Device slice (App Store / device builds). Simulator slice lives alongside
    # under ios-arm64_x86_64-simulator if a simulator build is ever wired up.
    set(TSNET_INCLUDE_DIR "${_tsnet_xcf}/ios-arm64/Headers" CACHE PATH "" FORCE)
    set(_tsnet_lib "${_tsnet_xcf}/ios-arm64/libtailscale_ios.a")
    if(NOT EXISTS "${_tsnet_lib}")
        message(FATAL_ERROR "tsnet: iOS libtailscale_ios.a missing after extract "
                            "(delete ${TSNET_DOWNLOAD_DIR} to re-download): ${_tsnet_lib}")
    endif()
    function(decenza_link_tsnet tgt)
        target_include_directories(${tgt} PRIVATE "${TSNET_INCLUDE_DIR}")
        target_link_libraries(${tgt} PRIVATE
            "${_tsnet_lib}"
            "-framework CoreFoundation"
            "-framework Security"
            "-framework Network")
    endfunction()

elseif(ANDROID)
    set(TSNET_INCLUDE_DIR "${_tsnet_extract}/libtailscale-android/include" CACHE PATH "" FORCE)
    set(_tsnet_so "${_tsnet_extract}/libtailscale-android/jniLibs/${CMAKE_ANDROID_ARCH_ABI}/libtailscale.so")
    if(NOT EXISTS "${_tsnet_so}")
        message(FATAL_ERROR "tsnet: no Android .so for ABI ${CMAKE_ANDROID_ARCH_ABI}")
    endif()
    function(decenza_link_tsnet tgt)
        target_include_directories(${tgt} PRIVATE "${TSNET_INCLUDE_DIR}")
        target_link_libraries(${tgt} PRIVATE "${_tsnet_so}")
        # Bundle the .so into the APK for the active ABI. The QT_ANDROID_EXTRA_LIBS
        # target property is the Qt6-idiomatic way (more reliable than the legacy
        # ANDROID_EXTRA_LIBS variable) — androiddeployqt copies it into the APK.
        set_property(TARGET ${tgt} APPEND PROPERTY QT_ANDROID_EXTRA_LIBS "${_tsnet_so}")
    endfunction()
endif()

message(STATUS "tsnet: enabled (${TSNET_TAG}) — include: ${TSNET_INCLUDE_DIR}")
