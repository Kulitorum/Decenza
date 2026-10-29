cmake_minimum_required(VERSION 3.24)
project(QtGcDiagnosticTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
enable_testing()
find_package(Qt6 6.12.0 EXACT REQUIRED COMPONENTS Core Bluetooth Qml QmlPrivate Test)
set(DECENZA_TEST_QML_FRAMEWORK_DIR "/private/tmp/decenza-qt-gc-test-build/lib" CACHE PATH
    "Directory containing the test-only QtQml framework")
add_executable(tst_qtgc_diagnostics tst_qtgc_diagnostics.cpp
    ../../../src/core/btlogfilter.cpp)
target_include_directories(tst_qtgc_diagnostics PRIVATE ../../../src)
target_link_libraries(tst_qtgc_diagnostics PRIVATE Qt6::Core Qt6::Bluetooth Qt6::Qml Qt6::QmlPrivate Qt6::Test)
target_compile_definitions(tst_qtgc_diagnostics PRIVATE
    DECENZA_TEST_QML_FRAMEWORK_DIR="${DECENZA_TEST_QML_FRAMEWORK_DIR}")
add_test(NAME tst_qtgc_diagnostics COMMAND tst_qtgc_diagnostics)
