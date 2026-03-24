enable_testing()
find_package(Qt6 REQUIRED COMPONENTS Test Core Bluetooth Sql Network)

# Convenience function for adding test executables.
# Each test compiles only the source files it needs (no full app build).
function(add_decenza_test name)
    add_executable(${name} ${ARGN})
    target_compile_definitions(${name} PRIVATE DECENZA_TESTING)
    target_link_libraries(${name} PRIVATE
        Qt6::Test Qt6::Core Qt6::Bluetooth Qt6::Sql Qt6::Network Qt6::Gui
    )
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}       # For mocks/
    )
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# Source file sets for reuse across tests
set(MACHINE_SOURCES
    ${CMAKE_SOURCE_DIR}/src/machine/machinestate.cpp
)

set(BLE_SOURCES
    ${CMAKE_SOURCE_DIR}/src/ble/de1device.cpp
    ${CMAKE_SOURCE_DIR}/src/ble/bletransport.cpp
    ${CMAKE_SOURCE_DIR}/src/ble/scaledevice.cpp
    ${CMAKE_SOURCE_DIR}/src/ble/protocol/binarycodec.cpp
)

set(PROFILE_SOURCES
    ${CMAKE_SOURCE_DIR}/src/profile/profile.cpp
    ${CMAKE_SOURCE_DIR}/src/profile/profileframe.cpp
    ${CMAKE_SOURCE_DIR}/src/profile/recipegenerator.cpp
    ${CMAKE_SOURCE_DIR}/src/profile/recipeanalyzer.cpp
    ${CMAKE_SOURCE_DIR}/src/profile/recipeparams.cpp
)

set(CORE_SOURCES
    ${CMAKE_SOURCE_DIR}/src/core/settings.cpp
)

set(CONTROLLER_SOURCES
    ${CMAKE_SOURCE_DIR}/src/controllers/shottimingcontroller.cpp
)

set(SIMULATOR_SOURCES
    ${CMAKE_SOURCE_DIR}/src/simulator/de1simulator.cpp
)

# --- tst_saw: WeightProcessor SAW tests (minimal deps) ---
add_decenza_test(tst_saw
    tst_saw.cpp
    ${CMAKE_SOURCE_DIR}/src/machine/weightprocessor.cpp
)

# --- tst_sav: MachineState SAV tests ---
# MockScaleDevice.h and de1transport.h have Q_OBJECT but no .cpp — need explicit MOC
qt6_wrap_cpp(TST_SAV_MOC_SOURCES
    mocks/MockScaleDevice.h
    ${CMAKE_SOURCE_DIR}/src/ble/de1transport.h
)
add_decenza_test(tst_sav
    tst_sav.cpp
    ${TST_SAV_MOC_SOURCES}
    ${MACHINE_SOURCES}
    ${BLE_SOURCES}
    ${PROFILE_SOURCES}
    ${CORE_SOURCES}
    ${CONTROLLER_SOURCES}
    ${SIMULATOR_SOURCES}
)
