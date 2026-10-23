# Exercise the production requester-side transfer component against minimal
# picker and import-service fixtures. Isolate this URI-identical test module
# from other targets in its own CMake binary directory.
set(_local_file_transfer_qml
    ${PROJECT_SOURCE_DIR}/qml/components/LocalFileTransfer.qml
    ${PROJECT_SOURCE_DIR}/qml/components/LocalFileDownload.qml
    ${CMAKE_CURRENT_SOURCE_DIR}/../qml/localfiletransfer/ImportService.qml
    ${CMAKE_CURRENT_SOURCE_DIR}/../qml/tst_localfiletransfer.qml
)
foreach(_file IN LISTS _local_file_transfer_qml)
    get_filename_component(_name "${_file}" NAME)
    set_source_files_properties("${_file}" PROPERTIES QT_RESOURCE_ALIAS "${_name}")
endforeach()
set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/../qml/localfiletransfer/ImportService.qml
    PROPERTIES QT_QML_SINGLETON_TYPE TRUE
)
add_executable(tst_localfiletransfer ../tst_localfiletransfer.cpp)
qt_add_qml_module(tst_localfiletransfer
    URI Decenza VERSION 1.0 RESOURCE_PREFIX /qt/qml
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/Decenza
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/mocklocalfileselection.h
    QML_FILES ${_local_file_transfer_qml}
)
target_link_libraries(tst_localfiletransfer PRIVATE Qt6::QuickTest Qt6::Quick Qt6::Qml)
set_property(GLOBAL APPEND PROPERTY DECENZA_TEST_TARGETS tst_localfiletransfer)
add_test(NAME tst_localfiletransfer COMMAND tst_localfiletransfer
    -input :/qt/qml/Decenza
    -o -,txt -o "${DECENZA_TEST_RESULTS_DIR}/tst_localfiletransfer.xml,junitxml")
set_tests_properties(tst_localfiletransfer PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=Basic"
    FAIL_REGULAR_EXPRESSION "Maximum amount of warnings exceeded")
