cmake_minimum_required(VERSION 3.24)
project(QtGcOwnershipRepro LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
enable_testing()
find_package(Qt6 6.12.0 EXACT REQUIRED COMPONENTS Core Network Qml QmlPrivate Quick QuickControls2 Test)
qt_policy(SET QTP0001 NEW)
add_executable(tst_qmlgc_ownership tst_qmlgc_ownership.cpp)
target_link_libraries(tst_qmlgc_ownership PRIVATE
    Qt6::Core Qt6::Network Qt6::Qml Qt6::QmlPrivate Qt6::Quick Qt6::QuickControls2 Qt6::Test)
target_compile_definitions(tst_qmlgc_ownership PRIVATE
    DECENZA_COMPONENTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../../qml/components")
set(_shared_controls)
foreach(_name SharedAccessibleButton SharedAccessibleTapArea SharedFocusIndicator
              SharedButtonStyle SharedButtonInteraction)
    set(_path "${CMAKE_CURRENT_SOURCE_DIR}/../../../qml/components/${_name}.qml")
    set_source_files_properties("${_path}" PROPERTIES QT_RESOURCE_ALIAS "${_name}.qml")
    list(APPEND _shared_controls "${_path}")
endforeach()
qt_add_qml_module(tst_qmlgc_ownership
    URI OwnershipProbe VERSION 1.0
    DEPENDENCIES QtQuick QtQuick.Templates QtQuick.Controls QtQuick.Effects
    SOURCES ProbeInteractionHost.h
    QML_FILES ReplayPage.qml ReplayStack.qml ${_shared_controls})
add_test(NAME tst_qmlgc_ownership COMMAND tst_qmlgc_ownership ownershipChange sharedButtonCreation)
set_tests_properties(tst_qmlgc_ownership PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
add_test(NAME tst_qmlgc_review COMMAND tst_qmlgc_ownership
    compiledReviewTransitions automaticReviewStress)
set_tests_properties(tst_qmlgc_review PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QV4_FAIL_ON_INVALID_AOT=1")
add_test(NAME tst_qmlgc_review_interpreted COMMAND tst_qmlgc_ownership automaticReviewStress)
set_tests_properties(tst_qmlgc_review_interpreted PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QV4_FORCE_INTERPRETER=1")
add_executable(tst_qmlgc_sweep tst_qmlgc_sweep.cpp)
target_link_libraries(tst_qmlgc_sweep PRIVATE Qt6::Qml Qt6::QmlPrivate Qt6::Test)
add_test(NAME tst_qmlgc_sweep COMMAND tst_qmlgc_sweep)
add_test(NAME tst_qmlgc_destruction COMMAND tst_qmlgc_ownership qmlDestructionAllocations)
set_tests_properties(tst_qmlgc_destruction PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
add_test(NAME tst_qmlgc_partial_scan COMMAND tst_qmlgc_ownership partialWeakScan)
set_tests_properties(tst_qmlgc_partial_scan PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QV4_FAIL_ON_INVALID_AOT=1")
add_test(NAME tst_qmlgc_async COMMAND tst_qmlgc_ownership asynchronousCreation)
set_tests_properties(tst_qmlgc_async PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QV4_FAIL_ON_INVALID_AOT=1")
add_test(NAME tst_qmlgc_xhr COMMAND tst_qmlgc_ownership xhrCallbackLifetime)
set_tests_properties(tst_qmlgc_xhr PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Optional syntax check of a separately staged Qt diagnostic patch. This object
# is never linked into the probes or installed over the SDK's QtQml library.
set(DECENZA_QT_DIAGNOSTIC_SOURCE "" CACHE PATH "Patched qtdeclarative source staging directory")
set(DECENZA_QT_REFERENCE_SOURCE "" CACHE PATH "Unmodified Qt 6.12.0 qtdeclarative source directory")
if(DECENZA_QT_DIAGNOSTIC_SOURCE AND DECENZA_QT_REFERENCE_SOURCE)
    add_library(qt_gc_history_compile OBJECT
        "${DECENZA_QT_DIAGNOSTIC_SOURCE}/src/qml/memory/qv4mm.cpp")
    set_target_properties(qt_gc_history_compile PROPERTIES AUTOMOC ON)
    target_link_libraries(qt_gc_history_compile PRIVATE Qt6::QmlPrivate)
    target_include_directories(qt_gc_history_compile PRIVATE
        "${DECENZA_QT_REFERENCE_SOURCE}/src/3rdparty/masm/stubs"
        "${DECENZA_QT_REFERENCE_SOURCE}/src/3rdparty/masm"
        "${DECENZA_QT_REFERENCE_SOURCE}/src/3rdparty/masm/wtf")
    target_compile_definitions(qt_gc_history_compile PRIVATE
        QT_BUILD_QML_LIB QT_NO_DEBUG WTF_EXPORT_PRIVATE=)
    target_compile_options(qt_gc_history_compile PRIVATE -include
        "${DECENZA_QT_DIAGNOSTIC_SOURCE}/src/qml/memory/qv4mmdefs_p.h")
    # Build the diagnostic assertions, but do not register a runnable CTest until
    # a complete QtQml framework containing the new patch is available.
    find_package(Qt6 REQUIRED COMPONENTS Bluetooth)
    add_executable(tst_qtgc_history_compile ../tests/tst_qtgc_diagnostics.cpp
        ../../../src/core/btlogfilter.cpp)
    target_include_directories(tst_qtgc_history_compile PRIVATE ../../../src)
    target_link_libraries(tst_qtgc_history_compile PRIVATE
        Qt6::Core Qt6::Bluetooth Qt6::Qml Qt6::QmlPrivate Qt6::Test)
    target_compile_definitions(tst_qtgc_history_compile PRIVATE
        DECENZA_TEST_QML_FRAMEWORK_DIR="${DECENZA_QT_DIAGNOSTIC_SOURCE}/build/lib")
endif()
