find_package(Qt6 REQUIRED COMPONENTS Test Core Bluetooth Sql Network)

# Disable AUTOMOC path prefix so MOC includes use plain filenames
# instead of relative paths that break for out-of-tree builds.
set(CMAKE_AUTOMOC_PATH_PREFIX OFF)

# Convenience function for adding test executables.
# Each test compiles only the source files it needs (no full app build).
function(add_decenza_test name)
    add_executable(${name} ${ARGN})
    set_target_properties(${name} PROPERTIES AUTOMOC_PATH_PREFIX OFF)
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
    ${CMAKE_SOURCE_DIR}/src/ble/blecapability.cpp
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

# --- tst_firmwarepackets: FWMapRequest + firmware chunk byte layout tests
# (header-only target; no cpp dependency beyond the test itself) ---
add_decenza_test(tst_firmwarepackets
    tst_firmwarepackets.cpp
)

# --- tst_firmwareheader: bootfwupdate.dat header parser + validator tests
# (header-only target; uses QFile / QTemporaryDir for on-disk cases) ---
add_decenza_test(tst_firmwareheader
    tst_firmwareheader.cpp
)

# --- tst_firmwareassetcachehelpers: pure helpers (MetaJson JSON round-trip,
# Range-header computation). Network-driven FirmwareAssetCache class is
# covered by the integration test in tst_firmwareflow. ---
add_decenza_test(tst_firmwareassetcachehelpers
    tst_firmwareassetcachehelpers.cpp
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

# --- tst_autoflowcal: window-level flow cal classification (#739 regression) ---
add_decenza_test(tst_autoflowcal
    tst_autoflowcal.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/autoflowcalclassifier.cpp
    ${CMAKE_SOURCE_DIR}/src/profile/profileframe.cpp
    ${CODEC_SOURCES}
)

# --- tst_machinestate: Phase transitions, volume reset, signals ---
add_decenza_test(tst_machinestate
    tst_machinestate.cpp
    mocks/MockScaleDevice.h
    ${CMAKE_SOURCE_DIR}/src/ble/de1transport.h
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
target_compile_definitions(tst_tclimport PRIVATE
    DE1APP_PROFILES_PATH="${CMAKE_CURRENT_SOURCE_DIR}/data/de1app_profiles"
)

# --- tst_recipegenerator: Frame generation vs de1app stock profiles ---
add_decenza_test(tst_recipegenerator
    tst_recipegenerator.cpp
    ${PROFILE_SOURCES}
    ${CODEC_SOURCES}
)

# --- tst_shotanalysis: ShotAnalysis quality heuristics ---
add_decenza_test(tst_shotanalysis
    tst_shotanalysis.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/shotanalysis.cpp
)

# --- tst_shotsettings: ShotSettings BLE wire format ---
add_decenza_test(tst_shotsettings
    tst_shotsettings.cpp
    mocks/MockTransport.h
    ${CMAKE_SOURCE_DIR}/src/ble/de1transport.h
    ${BLE_SOURCES}
    ${PROFILE_SOURCES}
    ${CORE_SOURCES}
    ${CONTROLLER_SOURCES}
    ${SIMULATOR_SOURCES}
)

# --- tst_mmrwrite: DE1Device::writeMMR per-register dedup (issue #783) ---
add_decenza_test(tst_mmrwrite
    tst_mmrwrite.cpp
    mocks/MockTransport.h
    ${CMAKE_SOURCE_DIR}/src/ble/de1transport.h
    ${BLE_SOURCES}
    ${PROFILE_SOURCES}
    ${CORE_SOURCES}
    ${CONTROLLER_SOURCES}
    ${SIMULATOR_SOURCES}
)

# --- tst_de1device_firmware: firmware-update BLE extensions on DE1Device ---
add_decenza_test(tst_de1device_firmware
    tst_de1device_firmware.cpp
    mocks/MockTransport.h
    ${CMAKE_SOURCE_DIR}/src/ble/de1transport.h
    ${BLE_SOURCES}
    ${PROFILE_SOURCES}
    ${CORE_SOURCES}
    ${CONTROLLER_SOURCES}
    ${SIMULATOR_SOURCES}
)

# --- tst_profileupload: Profile upload frame-ACK verification ---
add_decenza_test(tst_profileupload
    tst_profileupload.cpp
    mocks/MockTransport.h
    ${CMAKE_SOURCE_DIR}/src/ble/de1transport.h
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
    ${CMAKE_SOURCE_DIR}/src/ai/shotanalysis.cpp
    ${CMAKE_SOURCE_DIR}/src/models/shotdatamodel.cpp
    ${CMAKE_SOURCE_DIR}/src/rendering/fastlinerenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/network/visualizeruploader.cpp
    ${CMAKE_SOURCE_DIR}/src/profile/profilesavehelper.cpp
)

# History sources (ShotHistoryStorage + transitive deps)
set(HISTORY_SOURCES
    ${CMAKE_SOURCE_DIR}/src/history/shothistorystorage.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/shotanalysis.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/shotsummarizer.cpp
    ${CMAKE_SOURCE_DIR}/src/models/shotdatamodel.cpp
    ${CMAKE_SOURCE_DIR}/src/rendering/fastlinerenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/network/visualizeruploader.cpp
)

# --- tst_dbmigration: DB schema creation and migration chain ---
find_package(Qt6 REQUIRED COMPONENTS Quick)
add_decenza_test(tst_dbmigration
    tst_dbmigration.cpp
    ${CMAKE_SOURCE_DIR}/src/ble/de1transport.h
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
add_decenza_test(tst_scaleprotocol
    tst_scaleprotocol.cpp
    ${CMAKE_SOURCE_DIR}/src/ble/transport/scalebletransport.h
    ${CMAKE_SOURCE_DIR}/src/ble/scaledevice.cpp
    ${CMAKE_SOURCE_DIR}/src/ble/scales/decentscale.cpp
    ${CMAKE_SOURCE_DIR}/src/ble/scales/bookooscale.cpp
)

# --- tst_difluidr2: DiFluid R2 refractometer packet parsing and name matching ---
add_decenza_test(tst_difluidr2
    tst_difluidr2.cpp
    ${CMAKE_SOURCE_DIR}/src/ble/transport/scalebletransport.h
    ${CMAKE_SOURCE_DIR}/src/ble/refractometers/difluidr2.cpp
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

# --- tst_settling: SAW settling trimSettlingData(), m_sawSettling flag, shot save ordering ---
find_package(Qt6 REQUIRED COMPONENTS Charts Quick)
add_decenza_test(tst_settling
    tst_settling.cpp
    ${CMAKE_SOURCE_DIR}/src/models/shotdatamodel.cpp
    ${CMAKE_SOURCE_DIR}/src/rendering/fastlinerenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/ble/de1transport.h
    ${BLE_SOURCES}
    ${PROFILE_SOURCES}
    ${CORE_SOURCES}
    ${CONTROLLER_SOURCES}
    ${SIMULATOR_SOURCES}
)
target_link_libraries(tst_settling PRIVATE Qt6::Charts Qt6::Quick)

# --- tst_sav: MachineState SAV tests ---
add_decenza_test(tst_sav
    tst_sav.cpp
    mocks/MockScaleDevice.h
    ${CMAKE_SOURCE_DIR}/src/ble/de1transport.h
    ${MACHINE_SOURCES}
    ${BLE_SOURCES}
    ${PROFILE_SOURCES}
    ${CORE_SOURCES}
    ${CONTROLLER_SOURCES}
    ${SIMULATOR_SOURCES}
)

# --- Shared Q_OBJECT headers for tests using MockTransport (listed as sources for AUTOMOC) ---
set(MCP_MOC_HEADERS
    mocks/MockTransport.h
    ${CMAKE_SOURCE_DIR}/src/ble/de1transport.h
    ${CMAKE_SOURCE_DIR}/src/mcp/mcptoolregistry.h
    ${CMAKE_SOURCE_DIR}/src/mcp/mcpresourceregistry.h
)

# --- tst_profilemanager: ProfileManager lifecycle, signals, BLE, MCP resources, QML bindings ---
find_package(Qt6 REQUIRED COMPONENTS Charts Quick Qml)
add_decenza_test(tst_profilemanager
    tst_profilemanager.cpp
    ${MCP_MOC_HEADERS}
    ${PROFILEMANAGER_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/mcp/mcpresources.cpp
    ${CMAKE_SOURCE_DIR}/src/network/webdebuglogger.cpp
    ${CMAKE_SOURCE_DIR}/src/core/memorymonitor.cpp
    ${BLE_SOURCES}
    ${PROFILE_SOURCES}
    ${CORE_SOURCES}
    ${MACHINE_SOURCES}
    ${CONTROLLER_SOURCES}
    ${SIMULATOR_SOURCES}
    ${CMAKE_BINARY_DIR}/version_code.cpp
)
target_link_libraries(tst_profilemanager PRIVATE Qt6::Charts Qt6::Quick Qt6::Qml paho-mqtt3a-static)
target_include_directories(tst_profilemanager PRIVATE ${CMAKE_BINARY_DIR})
target_compile_definitions(tst_profilemanager PRIVATE SRCDIR="${CMAKE_SOURCE_DIR}/tests")
# Add built-in profiles QRC so isBuiltInFilename() works in tests
qt_add_resources(PROFILES_QRC ${CMAKE_SOURCE_DIR}/resources/profiles.qrc)
target_sources(tst_profilemanager PRIVATE ${PROFILES_QRC})
# Also needed for compareWithBuiltin test (QFile::exists(":/profiles/..."))
target_sources(tst_tclimport PRIVATE ${PROFILES_QRC})

# --- MCP tool tests (ProfileManager + MockTransport) ---

# --- tst_mcptools_profiles: Profile MCP tools, PR #561 BLE upload regression ---
find_package(Qt6 REQUIRED COMPONENTS Charts Quick)
add_decenza_test(tst_mcptools_profiles
    tst_mcptools_profiles.cpp
    ${MCP_MOC_HEADERS}
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
target_link_libraries(tst_mcptools_profiles PRIVATE Qt6::Charts Qt6::Quick paho-mqtt3a-static)
target_include_directories(tst_mcptools_profiles PRIVATE ${CMAKE_BINARY_DIR})

# --- tst_mcptools_write: Write MCP tools (settings_set temp/weight BLE upload) ---
# mcptools_write.cpp includes all write tools, pulling in Accessibility/Screensaver/Translation/Battery
find_package(Qt6 REQUIRED COMPONENTS TextToSpeech Multimedia)
add_decenza_test(tst_mcptools_write
    tst_mcptools_write.cpp
    ${MCP_MOC_HEADERS}
    ${CMAKE_SOURCE_DIR}/src/mcp/mcptools_write.cpp
    ${PROFILEMANAGER_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/core/accessibilitymanager.cpp
    ${CMAKE_SOURCE_DIR}/src/screensaver/screensavervideomanager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/translationmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/batterymanager.cpp
    ${CMAKE_SOURCE_DIR}/src/history/shothistorystorage.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/shotanalysis.cpp
    ${BLE_SOURCES}
    ${PROFILE_SOURCES}
    ${CORE_SOURCES}
    ${MACHINE_SOURCES}
    ${CONTROLLER_SOURCES}
    ${SIMULATOR_SOURCES}
    ${CMAKE_BINARY_DIR}/version_code.cpp
)
target_link_libraries(tst_mcptools_write PRIVATE Qt6::Charts Qt6::Quick Qt6::TextToSpeech Qt6::Multimedia paho-mqtt3a-static)
target_include_directories(tst_mcptools_write PRIVATE ${CMAKE_BINARY_DIR})

# --- tst_mcpserver_session: Session management, ping, subscribe/unsubscribe ---
# Uses real mcpserver.cpp but stubs out tool/resource registration functions
# to avoid linking mcptools_*.cpp and their heavy transitive deps.
find_package(Qt6 REQUIRED COMPONENTS Charts Quick)
add_decenza_test(tst_mcpserver_session
    tst_mcpserver_session.cpp
    ${MCP_MOC_HEADERS}
    ${CMAKE_SOURCE_DIR}/src/mcp/mcpserver.cpp
    ${CMAKE_SOURCE_DIR}/src/mcp/mcpserver.h
    ${CMAKE_SOURCE_DIR}/src/mcp/mcpsession.h
    ${PROFILEMANAGER_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/history/shothistorystorage.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/shotanalysis.cpp
    ${BLE_SOURCES}
    ${PROFILE_SOURCES}
    ${CORE_SOURCES}
    ${MACHINE_SOURCES}
    ${CONTROLLER_SOURCES}
    ${SIMULATOR_SOURCES}
    ${CMAKE_BINARY_DIR}/version_code.cpp
)
target_link_libraries(tst_mcpserver_session PRIVATE Qt6::Charts Qt6::Quick paho-mqtt3a-static)
target_include_directories(tst_mcpserver_session PRIVATE ${CMAKE_BINARY_DIR})

# --- tst_steamhealth: SteamDataModel summaries and SteamHealthTracker trend detection ---
add_decenza_test(tst_steamhealth
    tst_steamhealth.cpp
    ${CMAKE_SOURCE_DIR}/src/models/steamdatamodel.cpp
    ${CMAKE_SOURCE_DIR}/src/machine/steamhealthtracker.cpp
    ${CMAKE_SOURCE_DIR}/src/rendering/fastlinerenderer.cpp
)
target_link_libraries(tst_steamhealth PRIVATE Qt6::Quick Qt6::Charts)
