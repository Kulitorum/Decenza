# Pinned, unmodified sources. Static libraries keep packaging identical on all
# native platforms; no host zlib or private Qt ZIP API is required.
function(decenza_add_quazip)
    set(BUILD_SHARED_LIBS OFF)
    if(CMAKE_CXX_COMPILER_LAUNCHER AND NOT CMAKE_C_COMPILER_LAUNCHER)
        set(CMAKE_C_COMPILER_LAUNCHER "${CMAKE_CXX_COMPILER_LAUNCHER}")
    endif()
    if(NOT TARGET ZLIB::ZLIB)
        set(ZLIB_BUILD_SHARED OFF)
        set(ZLIB_BUILD_STATIC ON)
        set(ZLIB_BUILD_TESTING OFF)
        set(ZLIB_INSTALL OFF)
        FetchContent_Declare(zlib
            URL https://zlib.net/fossils/zlib-1.3.2.tar.gz
            URL_HASH SHA256=bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_MakeAvailable(zlib)
        add_library(ZLIB::ZLIB ALIAS zlibstatic)
        set(ZLIB_LIBRARY zlibstatic)
        set(ZLIB_LIBRARY_RELEASE zlibstatic)
        set(ZLIB_LIBRARY_DEBUG zlibstatic)
        set(ZLIB_INCLUDE_DIR "${zlib_SOURCE_DIR}")
    endif()
    set(QUAZIP_QT_MAJOR_VERSION 6)
    set(QUAZIP_ENABLE_QTEXTCODEC OFF)
    set(QUAZIP_BZIP2 OFF)
    set(QUAZIP_FETCH_LIBS OFF)
    set(QUAZIP_INSTALL OFF)
    set(QUAZIP_ENABLE_TESTS OFF)
    FetchContent_Declare(quazip
        URL https://github.com/stachenov/quazip/archive/refs/tags/v1.6.tar.gz
        URL_HASH SHA256=886126d2b1f803c9699af9d78bc7643e8268f3621f1aaa58e76f7a486161156d
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(quazip)
    set(_licenses "${quazip_SOURCE_DIR}/COPYING")
    if(TARGET zlibstatic)
        list(APPEND _licenses "${zlib_SOURCE_DIR}/LICENSE")
        set_source_files_properties("${zlib_SOURCE_DIR}/LICENSE"
            PROPERTIES QT_RESOURCE_ALIAS "zlib-LICENSE")
    endif()
    set(DECENZA_ZIP_LICENSES "${_licenses}" PARENT_SCOPE)
    set_source_files_properties("${quazip_SOURCE_DIR}/COPYING"
        PROPERTIES QT_RESOURCE_ALIAS "QuaZip-COPYING")
endfunction()
decenza_add_quazip()
