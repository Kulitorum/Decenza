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

# Standalone codec source (for lightweight tests that only need BLE encoding)
set(CODEC_SOURCES
    ${CMAKE_SOURCE_DIR}/src/ble/protocol/binarycodec.cpp
)

# --- tst_binarycodec: BLE codec encode/decode tests (minimal deps) ---
add_decenza_test(tst_binarycodec
    tst_binarycodec.cpp
    ${CODEC_SOURCES}
)

# --- tst_profile: Profile JSON/TCL parsing, frame generation, BLE encoding ---
add_decenza_test(tst_profile
    tst_profile.cpp
    ${PROFILE_SOURCES}
    ${CODEC_SOURCES}
)

# --- tst_profileframe: ProfileFrame JSON/TCL/flags tests (minimal deps) ---
add_decenza_test(tst_profileframe
    tst_profileframe.cpp
    ${CMAKE_SOURCE_DIR}/src/profile/profileframe.cpp
    ${CODEC_SOURCES}
)

# --- tst_machinestate: Phase transitions, volume reset, signals ---
qt6_wrap_cpp(TST_MACHINESTATE_MOC_SOURCES
    mocks/MockScaleDevice.h
    ${CMAKE_SOURCE_DIR}/src/ble/de1transport.h
)
add_decenza_test(tst_machinestate
    tst_machinestate.cpp
    ${TST_MACHINESTATE_MOC_SOURCES}
    ${MACHINE_SOURCES}
    ${BLE_SOURCES}
    ${PROFILE_SOURCES}
    ${CORE_SOURCES}
    ${CONTROLLER_SOURCES}
    ${SIMULATOR_SOURCES}
)

# --- tst_recipeparams: RecipeParams serialization, validation, clamping ---
add_decenza_test(tst_recipeparams
    tst_recipeparams.cpp
    ${CMAKE_SOURCE_DIR}/src/profile/recipeparams.cpp
)

# --- tst_settings: Settings persistence and defaults ---
add_decenza_test(tst_settings
    tst_settings.cpp
    ${CORE_SOURCES}
)

# --- tst_tclimport: TCL profile import round-trip against de1app profiles ---
add_decenza_test(tst_tclimport
    tst_tclimport.cpp
    ${PROFILE_SOURCES}
    ${CODEC_SOURCES}
)

# --- tst_recipegenerator: Frame generation vs de1app stock profiles ---
add_decenza_test(tst_recipegenerator
    tst_recipegenerator.cpp
    ${PROFILE_SOURCES}
    ${CODEC_SOURCES}
)

# --- tst_shotsettings: ShotSettings BLE wire format ---
qt6_wrap_cpp(TST_SHOTSETTINGS_MOC_SOURCES
    mocks/MockTransport.h
    ${CMAKE_SOURCE_DIR}/src/ble/de1transport.h
)
add_decenza_test(tst_shotsettings
    tst_shotsettings.cpp
    ${TST_SHOTSETTINGS_MOC_SOURCES}
    ${BLE_SOURCES}
    ${PROFILE_SOURCES}
    ${CORE_SOURCES}
    ${CONTROLLER_SOURCES}
    ${SIMULATOR_SOURCES}
)

# --- tst_blefidelity: BLE encode round-trip for all built-in profiles ---
add_decenza_test(tst_blefidelity
    tst_blefidelity.cpp
    ${PROFILE_SOURCES}
    ${CODEC_SOURCES}
)
target_compile_definitions(tst_blefidelity PRIVATE
    SRCDIR="${CMAKE_CURRENT_SOURCE_DIR}"
    PROFILES_DIR="${CMAKE_SOURCE_DIR}/resources/profiles"
)

# ProfileManager sources (for MCP tool tests)
set(PROFILEMANAGER_SOURCES
    ${CMAKE_SOURCE_DIR}/src/controllers/profilemanager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/profilestorage.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/shotsummarizer.cpp
    ${CMAKE_SOURCE_DIR}/src/models/shotdatamodel.cpp
    ${CMAKE_SOURCE_DIR}/src/rendering/fastlinerenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/network/visualizeruploader.cpp
)

# History sources (ShotHistoryStorage + transitive deps)
set(HISTORY_SOURCES
    ${CMAKE_SOURCE_DIR}/src/history/shothistorystorage.cpp
    ${CMAKE_SOURCE_DIR}/src/models/shotdatamodel.cpp
    ${CMAKE_SOURCE_DIR}/src/rendering/fastlinerenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/network/visualizeruploader.cpp
)

# --- tst_dbmigration: DB schema creation and migration chain ---
find_package(Qt6 REQUIRED COMPONENTS Quick)
qt6_wrap_cpp(TST_DBMIGRATION_MOC_SOURCES
    ${CMAKE_SOURCE_DIR}/src/ble/de1transport.h
)
add_decenza_test(tst_dbmigration
    tst_dbmigration.cpp
    ${TST_DBMIGRATION_MOC_SOURCES}
    ${HISTORY_SOURCES}
    ${BLE_SOURCES}
    ${PROFILE_SOURCES}
    ${CORE_SOURCES}
    ${CONTROLLER_SOURCES}
    ${SIMULATOR_SOURCES}
    ${CMAKE_BINARY_DIR}/version_code.cpp
)
target_link_libraries(tst_dbmigration PRIVATE Qt6::Charts Qt6::Quick)
target_include_directories(tst_dbmigration PRIVATE ${CMAKE_BINARY_DIR})

# --- tst_scaleprotocol: Scale BLE packet parsing (Decent, Bookoo) ---
qt6_wrap_cpp(TST_SCALEPROTOCOL_MOC_SOURCES
    ${CMAKE_SOURCE_DIR}/src/ble/transport/scalebletransport.h
)
add_decenza_test(tst_scaleprotocol
    tst_scaleprotocol.cpp
    ${TST_SCALEPROTOCOL_MOC_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/ble/scaledevice.cpp
    ${CMAKE_SOURCE_DIR}/src/ble/scales/decentscale.cpp
    ${CMAKE_SOURCE_DIR}/src/ble/scales/bookooscale.cpp
)

# --- tst_weightprocessor: WeightProcessor edge cases (LSLR, oscillation, per-frame) ---
add_decenza_test(tst_weightprocessor
    tst_weightprocessor.cpp
    ${CMAKE_SOURCE_DIR}/src/machine/weightprocessor.cpp
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

# --- MCP tool tests (ProfileManager + MockTransport) ---
# Shared MOC for all MCP tests (MockTransport.h and de1transport.h have Q_OBJECT)
qt6_wrap_cpp(TST_MCP_MOC_SOURCES
    mocks/MockTransport.h
    ${CMAKE_SOURCE_DIR}/src/ble/de1transport.h
    ${CMAKE_SOURCE_DIR}/src/mcp/mcptoolregistry.h
)

# --- tst_mcptools_profiles: Profile MCP tools, PR #561 BLE upload regression ---
find_package(Qt6 REQUIRED COMPONENTS Charts Quick)
add_decenza_test(tst_mcptools_profiles
    tst_mcptools_profiles.cpp
    ${TST_MCP_MOC_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/mcp/mcptools_profiles.cpp
    ${PROFILEMANAGER_SOURCES}
    ${BLE_SOURCES}
    ${PROFILE_SOURCES}
    ${CORE_SOURCES}
    ${MACHINE_SOURCES}
    ${CONTROLLER_SOURCES}
    ${SIMULATOR_SOURCES}
    ${CMAKE_BINARY_DIR}/version_code.cpp
)
target_link_libraries(tst_mcptools_profiles PRIVATE Qt6::Charts Qt6::Quick)
target_include_directories(tst_mcptools_profiles PRIVATE ${CMAKE_BINARY_DIR})

# --- tst_mcptools_write: Write MCP tools (settings_set temp/weight BLE upload) ---
# mcptools_write.cpp includes all write tools, pulling in Accessibility/Screensaver/Translation/Battery
find_package(Qt6 REQUIRED COMPONENTS TextToSpeech Multimedia)
add_decenza_test(tst_mcptools_write
    tst_mcptools_write.cpp
    ${TST_MCP_MOC_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/mcp/mcptools_write.cpp
    ${PROFILEMANAGER_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/core/accessibilitymanager.cpp
    ${CMAKE_SOURCE_DIR}/src/screensaver/screensavervideomanager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/translationmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/batterymanager.cpp
    ${CMAKE_SOURCE_DIR}/src/history/shothistorystorage.cpp
    ${BLE_SOURCES}
    ${PROFILE_SOURCES}
    ${CORE_SOURCES}
    ${MACHINE_SOURCES}
    ${CONTROLLER_SOURCES}
    ${SIMULATOR_SOURCES}
    ${CMAKE_BINARY_DIR}/version_code.cpp
)
target_link_libraries(tst_mcptools_write PRIVATE Qt6::Charts Qt6::Quick Qt6::TextToSpeech Qt6::Multimedia)
target_include_directories(tst_mcptools_write PRIVATE ${CMAKE_BINARY_DIR})
