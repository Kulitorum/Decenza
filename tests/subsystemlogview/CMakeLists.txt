# Exercise the production SubsystemLogView component against a deterministic
# host-service fixture. Keep the module URI Decenza so the shared component's
# imports resolve as they do in the app; this subdirectory isolates Qt's
# URI-named generated qmldir resource from other Decenza test modules.
set(_system_log_view_qml
    ${PROJECT_SOURCE_DIR}/qml/components/SubsystemLogView.qml
    ${CMAKE_CURRENT_SOURCE_DIR}/../qml/systemlogview/AccessibleButton.qml
    ${CMAKE_CURRENT_SOURCE_DIR}/../qml/systemlogview/Theme.qml
    ${CMAKE_CURRENT_SOURCE_DIR}/../qml/systemlogview/TranslationManager.qml
    ${CMAKE_CURRENT_SOURCE_DIR}/../qml/tst_subsystemlogview.qml
)
foreach(_file IN LISTS _system_log_view_qml)
    get_filename_component(_name "${_file}" NAME)
    set_source_files_properties("${_file}" PROPERTIES QT_RESOURCE_ALIAS "${_name}")
endforeach()
set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/../qml/systemlogview/Theme.qml
    ${CMAKE_CURRENT_SOURCE_DIR}/../qml/systemlogview/TranslationManager.qml
    PROPERTIES QT_QML_SINGLETON_TYPE TRUE
)
add_executable(tst_subsystemlogview ../tst_subsystemlogview.cpp)
qt_add_qml_module(tst_subsystemlogview
    URI Decenza VERSION 1.0 RESOURCE_PREFIX /qt/qml
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/Decenza
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../systemlogviewtestservice.h
    QML_FILES ${_system_log_view_qml}
)
target_include_directories(tst_subsystemlogview PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(tst_subsystemlogview PRIVATE
    Qt6::QuickTest Qt6::Quick Qt6::Qml Qt6::QuickControls2
)
set_property(GLOBAL APPEND PROPERTY DECENZA_TEST_TARGETS tst_subsystemlogview)
add_test(NAME tst_subsystemlogview COMMAND tst_subsystemlogview
    -input :/qt/qml/Decenza
    -o -,txt -o "${DECENZA_TEST_RESULTS_DIR}/tst_subsystemlogview.xml,junitxml")
set_tests_properties(tst_subsystemlogview PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=Basic"
    FAIL_REGULAR_EXPRESSION "Maximum amount of warnings exceeded")
