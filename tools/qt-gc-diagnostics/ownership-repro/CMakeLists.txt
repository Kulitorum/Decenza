cmake_minimum_required(VERSION 3.24)
project(QtGcOwnershipRepro LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
enable_testing()
find_package(Qt6 6.12.0 EXACT REQUIRED COMPONENTS Core Qml QmlPrivate Quick QuickControls2 Test)
add_executable(tst_qmlgc_ownership tst_qmlgc_ownership.cpp)
target_link_libraries(tst_qmlgc_ownership PRIVATE
    Qt6::Core Qt6::Qml Qt6::QmlPrivate Qt6::Quick Qt6::QuickControls2 Qt6::Test)
target_compile_definitions(tst_qmlgc_ownership PRIVATE
    DECENZA_COMPONENTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../../qml/components")
add_test(NAME tst_qmlgc_ownership COMMAND tst_qmlgc_ownership)
set_tests_properties(tst_qmlgc_ownership PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
