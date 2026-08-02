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
# To adopt a newer libtailscale build, bump _TSNET_EXPECTED_TAG *and* the three
# hashes below, together — they are one unit. See the guard directly under them.

# The tag this source tree expects. Not a cache variable: it is the pin, and a
# stale build directory must not be able to silently disagree with it.
set(_TSNET_EXPECTED_TAG "decenza-v1.94.1-5")

set(TSNET_TAG "${_TSNET_EXPECTED_TAG}" CACHE STRING "libtailscale prebuilt release tag")

# Guard: a cached TSNET_TAG wins over the default above, so bumping the pin does
# NOTHING to an existing build directory — it keeps downloading and linking the
# OLD library while the source says otherwise. That is a silent wrong-artifact
# build, and it survived four tag bumps unnoticed: verifying decenza-v1.94.1-5
# locally initially "passed" against the -4 archive still named in the cache.
#
# It cannot be fixed by just re-reading the file, because the SHA-256s below are
# plain variables while the tag is cached — so a mismatched pair doesn't even
# fail honestly: an intentional `-DTSNET_TAG=<other>` against this tree's hashes
# dies as `download ... failed`, which reads like a network problem.
#
# So refuse, and say exactly what to run. CI is unaffected (it configures fresh
# build dirs; no workflow caches CMakeCache.txt) — this only ever fires locally.
if(NOT TSNET_TAG STREQUAL _TSNET_EXPECTED_TAG)
    message(FATAL_ERROR
        "tsnet: this source tree pins ${_TSNET_EXPECTED_TAG}, but this build "
        "directory has TSNET_TAG=${TSNET_TAG} cached.\n"
        "The hashes in cmake/tsnet.cmake belong to ${_TSNET_EXPECTED_TAG}, so "
        "building against ${TSNET_TAG} would link the wrong libtailscale.\n"
        "Fix the build directory:\n"
        "    cmake -DTSNET_TAG=${_TSNET_EXPECTED_TAG} ${CMAKE_BINARY_DIR}\n"
        "To deliberately test a different release, change _TSNET_EXPECTED_TAG "
        "and its three SHA-256s in cmake/tsnet.cmake instead — they are one unit.")
endif()
set(TSNET_BASE_URL "https://github.com/skialpine/libtailscale/releases/download/${TSNET_TAG}")
set(TSNET_DOWNLOAD_DIR "${CMAKE_BINARY_DIR}/tsnet-${TSNET_TAG}")

# Per-platform artifact + expected SHA-256 (from manifest.json).
if(IOS)
    set(_tsnet_zip "libtailscale-ios.zip")
    set(_tsnet_sha "706cbbd25d60dce49f5935018931a500e48cd4d498847294eeafdc6efbb0b739")
elseif(ANDROID)
    set(_tsnet_zip "libtailscale-android.zip")
    set(_tsnet_sha "80331572de0565b0be2465b67d8839efb564008281776393430b19d85cbdb3e0")
elseif(APPLE)
    set(_tsnet_zip "libtailscale-macos.zip")
    set(_tsnet_sha "9e6b42c895a7cd660c1233aead0c693417033a431b93450b79af9c0aeafc3582")
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
