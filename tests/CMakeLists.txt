find_package(Qt6 REQUIRED COMPONENTS Test Core Bluetooth Sql Network WebSockets)

# Every test links decenza_testlib directly (see add_decenza_test), and the
# targets that also link decenza_shotlib get it a second time through that
# library's PUBLIC interface. Without de-duplication the Apple linker emits
# "ignoring duplicate libraries" for each such target.
if(POLICY CMP0156)
    cmake_policy(SET CMP0156 NEW)
endif()

# Disable AUTOMOC path prefix so MOC includes use plain filenames
# instead of relative paths that break for out-of-tree builds.
set(CMAKE_AUTOMOC_PATH_PREFIX OFF)

# Convenience function for adding test executables.
# Each test compiles only the source files it needs (no full app build).
function(add_decenza_test name)
    add_executable(${name} ${ARGN})
    # Record every test executable so `build_tests` below can depend on the set
    # without anyone maintaining a second list by hand.
    #
    # A target declared with a bare add_executable + add_test does NOT pass
    # through here and must append to DECENZA_TEST_TARGETS itself — see
    # tst_shotcorpus. Forgetting that shows up as "(Not Run)" in a clean build,
    # and ONLY in a clean build.
    set_property(GLOBAL APPEND PROPERTY DECENZA_TEST_TARGETS ${name})
    set_target_properties(${name} PROPERTIES AUTOMOC_PATH_PREFIX OFF)
    target_compile_definitions(${name} PRIVATE DECENZA_TESTING)
    # Source-tree path, so a test can assert against shipped QML/assets rather than
    # a duplicated copy of their contents (see themeQmlFontRoleNamesMatchDefaults).
    target_compile_definitions(${name} PRIVATE
        DECENZA_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    # Link the shared decenza_testlib so the base sources (settings, BLE,
    # profile, machine, …) are compiled once, not recompiled into every target.
    # Per-process QSettings isolation is handled in the settings classes
    # themselves via Settings::testQSettingsPath() (a PID-scoped IniFormat store)
    # under DECENZA_TESTING — that is what makes `ctest -j` safe.
    target_link_libraries(${name} PRIVATE
        decenza_testlib
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
    ${CMAKE_SOURCE_DIR}/src/profile/temperaturedisplay.cpp
)

set(CORE_SOURCES
    ${CMAKE_SOURCE_DIR}/src/core/emojiassets.cpp
    ${CMAKE_SOURCE_DIR}/src/core/markdownrenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/core/settings.cpp
    ${CMAKE_SOURCE_DIR}/src/core/settings_mqtt.cpp
    ${CMAKE_SOURCE_DIR}/src/core/settings_autowake.cpp
    ${CMAKE_SOURCE_DIR}/src/core/settings_hardware.cpp
    ${CMAKE_SOURCE_DIR}/src/core/settings_ai.cpp
    ${CMAKE_SOURCE_DIR}/src/core/settings_theme.cpp
    ${CMAKE_SOURCE_DIR}/src/core/backgroundpresets.cpp
    ${CMAKE_SOURCE_DIR}/src/core/settings_visualizer.cpp
    ${CMAKE_SOURCE_DIR}/src/core/settings_mcp.cpp
    ${CMAKE_SOURCE_DIR}/src/core/settings_brew.cpp
    ${CMAKE_SOURCE_DIR}/src/core/settings_dye.cpp
    ${CMAKE_SOURCE_DIR}/src/history/coffeebagstorage.cpp  # SettingsDye write-through dependency
    ${CMAKE_SOURCE_DIR}/src/history/equipmentstorage.cpp
    ${CMAKE_SOURCE_DIR}/src/core/settings_network.cpp
    ${CMAKE_SOURCE_DIR}/src/core/settings_app.cpp
    ${CMAKE_SOURCE_DIR}/src/core/settings_calibration.cpp
    ${CMAKE_SOURCE_DIR}/src/ble/scales/scaletypeids.cpp
)

set(CONTROLLER_SOURCES
    ${CMAKE_SOURCE_DIR}/src/controllers/shottimingcontroller.cpp
)

# Shot analysis / projection layer. Plain Qt Core — no Quick or Graphs — so it
# belongs in decenza_testlib and lightweight tests (tst_shotprojection,
# tst_shotanalysis) can use it without dragging in the GUI frameworks. The
# Quick-dependent half lives in decenza_shotlib below.
set(ANALYSIS_SOURCES
    ${CMAKE_SOURCE_DIR}/src/history/shotprojection.cpp
    # shotprojection.h is listed so AUTOMOC generates the Q_GADGET meta-object —
    # without it ShotProjection::staticMetaObject is undefined and gadget users
    # fail to link.
    ${CMAKE_SOURCE_DIR}/src/history/shotprojection.h
    ${CMAKE_SOURCE_DIR}/src/ai/conductance.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/shotanalysis.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/shotsummarizer.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/shotsummarizer_kb.cpp
)

set(SIMULATOR_SOURCES
    ${CMAKE_SOURCE_DIR}/src/simulator/de1simulator.cpp
)

# Standalone codec source (for lightweight tests that only need BLE encoding)
set(CODEC_SOURCES
    ${CMAKE_SOURCE_DIR}/src/ble/protocol/binarycodec.cpp
)

# --- tst_binarycodec: BLE codec encode/decode tests (minimal deps) ---

# ---------------------------------------------------------------------------
# decenza_testlib: the base-Qt sources that most test binaries share, compiled
# ONCE here instead of recompiled into each of ~30 targets (CORE alone was
# compiled 29×). Test targets link this instead of listing the bundle vars.
# Built with DECENZA_TESTING so the friend-class seams match, and AUTOMOC on so
# Q_OBJECT sources (de1device, machinestate, de1transport.h, …) are moc'd once.
# ---------------------------------------------------------------------------
add_library(decenza_testlib STATIC
    ${MACHINE_SOURCES}
    ${BLE_SOURCES}
    ${PROFILE_SOURCES}
    ${CORE_SOURCES}
    ${CONTROLLER_SOURCES}
    ${SIMULATOR_SOURCES}
    ${ANALYSIS_SOURCES}
    # TranslationManager is needed by sources that link this lib (VisualizerUploader
    # via decenza_shotlib; LiveSteamCoach) now that they route user-visible strings
    # through translate(). Compiling it into the lib provides the symbol once
    # instead of each target adding translationmanager.cpp.
    ${CMAKE_SOURCE_DIR}/src/core/translationmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/ble/de1transport.h
)
set_target_properties(decenza_testlib PROPERTIES AUTOMOC ON AUTOMOC_PATH_PREFIX OFF)
target_compile_definitions(decenza_testlib PUBLIC DECENZA_TESTING)
# Qt6::Qml is needed for QJSValue/QJSEngine: TranslationManager exposes `translate` as a
# QJSValue property holding a callable, which is what makes translated bindings re-evaluate
# on a language change (see translationmanager.h). PUBLIC so every test target inherits it.
find_package(Qt6 REQUIRED COMPONENTS Qml)
target_link_libraries(decenza_testlib PUBLIC
    Qt6::Core Qt6::Bluetooth Qt6::Sql Qt6::Network Qt6::Gui Qt6::Qml
)
target_include_directories(decenza_testlib PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# ---------------------------------------------------------------------------
# decenza_shotlib: the shot-history / live-model / upload layer, compiled ONCE
# instead of into every target that needs it. Split from decenza_testlib because
# it drags in Qt Quick, which the small tests (tst_binarycodec, tst_shotprojection)
# have no reason to link. The Quick edge is fastlinerenderer.h (QQuickItem,
# QSGGeometryNode), included by shotdatamodel.cpp and fastlinerenderer.cpp —
# NOT shotdatamodel.h, which is Qt Core only and forward-declares the renderer.
# Link this instead of listing the sources; the plain-Core analysis/projection
# half is in decenza_testlib via ANALYSIS_SOURCES.
#
# Tests that link this still need version_code.cpp and, if they touch the KB
# loaders, the ai.qrc resource — neither belongs in a shared static lib.
# ---------------------------------------------------------------------------
find_package(Qt6 REQUIRED COMPONENTS Quick)
add_library(decenza_shotlib STATIC
    ${CMAKE_SOURCE_DIR}/src/history/shothistorystorage.cpp
    ${CMAKE_SOURCE_DIR}/src/history/shothistorystorage_internal.cpp
    ${CMAKE_SOURCE_DIR}/src/history/shothistorystorage_serialize.cpp
    ${CMAKE_SOURCE_DIR}/src/history/shothistorystorage_queries.cpp
    ${CMAKE_SOURCE_DIR}/src/history/recipestorage.cpp
    ${CMAKE_SOURCE_DIR}/src/models/shotdatamodel.cpp
    ${CMAKE_SOURCE_DIR}/src/rendering/fastlinerenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/network/visualizeruploader.cpp
)
set_target_properties(decenza_shotlib PROPERTIES AUTOMOC ON AUTOMOC_PATH_PREFIX OFF)
target_compile_definitions(decenza_shotlib PUBLIC DECENZA_TESTING)
target_link_libraries(decenza_shotlib PUBLIC
    decenza_testlib
    Qt6::Quick
)
# ${CMAKE_BINARY_DIR} for the generated version.h, which visualizeruploader.cpp
# includes. PUBLIC so linking targets inherit it and don't each repeat it.
target_include_directories(decenza_shotlib PUBLIC ${CMAKE_BINARY_DIR})

add_decenza_test(tst_binarycodec
    tst_binarycodec.cpp
)

# --- tst_aborted_shot_classifier: header-only "did the shot start?" classifier (issue #899) ---
add_decenza_test(tst_aborted_shot_classifier
    tst_aborted_shot_classifier.cpp
)

# --- tst_tastecvamap: header-only taste-tap → Visualizer CVA mapping (add-ai-taste-intake) ---
add_decenza_test(tst_tastecvamap
    tst_tastecvamap.cpp
)

# --- tst_visualizershotlist: header-only shared paginator decision logic
# (window filter / newest-first early-stop / page-ceiling guard) ---
add_decenza_test(tst_visualizershotlist
    tst_visualizershotlist.cpp
)

# --- tst_dialing_helpers: pure session-grouping algorithm (issue #1009) ---
add_decenza_test(tst_dialing_helpers
    tst_dialing_helpers.cpp
)

# --- tst_basketaliases: basket registry data-integrity + lookup/summary
# helpers (header-only target; mirrors the untested GrinderAliases sibling) ---
add_decenza_test(tst_basketaliases
    tst_basketaliases.cpp
)

# --- tst_drinktypes: recipe-wizard tea profile type-matching, temp proximity,
# and per-type default temps (header-only target; add-recipe-wizard-tea) ---
add_decenza_test(tst_drinktypes
    tst_drinktypes.cpp
)

# --- tst_recipeselectionmodel: pure selection + deferred-start state machine
# behind MainController::selectedRecipeId (header-only target; PR #1470) ---
add_decenza_test(tst_recipeselectionmodel
    tst_recipeselectionmodel.cpp
)

# --- tst_mcptools_shots_helpers: detectorResults envelope reshape (issue #1012) ---
add_decenza_test(tst_mcptools_shots_helpers
    tst_mcptools_shots_helpers.cpp
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

# --- tst_migrationinterfaceselect: pure interface-selection ordering for
# multi-homing-robust device-migration connects. orderedSubnetCandidates() is
# inline in datamigrationclient.h, so this needs no client/storage sources. ---
add_decenza_test(tst_migrationinterfaceselect
    tst_migrationinterfaceselect.cpp
)

# --- tst_profile: Profile JSON/TCL parsing, frame generation, BLE encoding ---
add_decenza_test(tst_profile
    tst_profile.cpp
)

# --- tst_visualizershotparse: JSON->ShotRecord recovery parser (string-encoded
# DYE scalars must not zero; hollow-shot guard; phantom-frame guard). ---
add_decenza_test(tst_visualizershotparse
    tst_visualizershotparse.cpp
    ${CMAKE_SOURCE_DIR}/src/history/shotfileparser.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/shotanalysis.cpp
)
target_compile_definitions(tst_visualizershotparse PRIVATE
    TST_VIS_PARSE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

# --- tst_temperaturedisplay: adaptive temp-override display formatter ---
add_decenza_test(tst_temperaturedisplay
    tst_temperaturedisplay.cpp
)

# --- tst_profileframe: ProfileFrame JSON/TCL/flags tests (minimal deps) ---
add_decenza_test(tst_profileframe
    tst_profileframe.cpp
)

# --- tst_autoflowcal: window-level flow cal classification (#739 regression) ---
add_decenza_test(tst_autoflowcal
    tst_autoflowcal.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/autoflowcalclassifier.cpp
)

# --- tst_machinestate: Phase transitions, volume reset, signals ---
add_decenza_test(tst_machinestate
    tst_machinestate.cpp
    mocks/MockScaleDevice.h
)

# --- tst_livesteamcoach: during-steam coaching milestones, gating, completion ---
add_decenza_test(tst_livesteamcoach
    tst_livesteamcoach.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/livesteamcoach.cpp
)

# --- tst_machinestatussnapshot: widget snapshot schema + temp coalescing ---
add_decenza_test(tst_machinestatussnapshot
    tst_machinestatussnapshot.cpp
    mocks/MockScaleDevice.h
    ${CMAKE_SOURCE_DIR}/src/widget/machinestatussnapshot.cpp
)

# --- tst_recipeparams: RecipeParams serialization, validation, clamping ---
add_decenza_test(tst_recipeparams
    tst_recipeparams.cpp
)

# --- tst_shotprojection: ShotProjection::coerce() (QVariant → gadget) ---
add_decenza_test(tst_shotprojection
    tst_shotprojection.cpp
)

# --- tst_roastdate: RoastDate::toIso() roast-date normalizer (header-only) ---
add_decenza_test(tst_roastdate
    tst_roastdate.cpp
)

# --- tst_settings: Settings persistence and defaults ---
add_decenza_test(tst_settings
    tst_settings.cpp
    ${CMAKE_SOURCE_DIR}/src/core/settingsserializer.cpp
)

# --- tst_backgroundpresets: background preset catalogue + Glass theme ---
# Includes the WCAG contrast floors for every colour — against the page, the page under
# the densest pattern, the card and the action tile — computable here in a way it never
# was for a photo background, and measured against the shipped derivation in C++.
add_decenza_test(tst_backgroundpresets
    tst_backgroundpresets.cpp
    # For the backup/migration round-trip: the colour and pattern must survive a
    # settings export/import, which backs every persistence surface in the app.
    ${CMAKE_SOURCE_DIR}/src/core/settingsserializer.cpp
)
# Compile the production qrc so :/backgrounds/*.svg resolve — the catalogue test asserts
# every pattern asset actually ships, which is the check that catches a catalogue entry
# pointing at a file nobody added to resources.qrc.
target_sources(tst_backgroundpresets PRIVATE ${CMAKE_SOURCE_DIR}/resources/resources.qrc)
set_target_properties(tst_backgroundpresets PROPERTIES AUTORCC ON)
# Qt6::Svg so the test can RENDER each pattern tile and measure its ink coverage, rather
# than trusting the hand-entered figure the contrast maths depends on. Two of the six were
# wrong before this check existed, both understating the shift.
target_link_libraries(tst_backgroundpresets PRIVATE Qt6::Svg)

# --- tst_saw_settings: per-(profile, scale) SAW learning + batch + bootstrap ---
add_decenza_test(tst_saw_settings
    tst_saw_settings.cpp
)

# --- tst_translationreactivity: QML binding dependency on translate/emoji lookups ---
# Needs a real QQmlEngine to assert that bindings re-evaluate, so it links Qt6::Qml
# on top of what add_decenza_test provides. Guards the mechanism that keeps 3,248
# translated strings from silently freezing after a language change. TranslationManager
# itself comes from decenza_testlib, so realTranslateIsANotifyingProperty() checks the
# SHIPPING meta-object rather than the spike's stand-in.
find_package(Qt6 REQUIRED COMPONENTS Qml)
add_decenza_test(tst_translationreactivity
    tst_translationreactivity.cpp
)
target_link_libraries(tst_translationreactivity PRIVATE Qt6::Qml)

# --- tst_translationsourcedrift: what happens when a key's English is rewritten ---
# Pins the contract that the registry follows the source text while the existing translation is
# KEPT and merely reported, not dropped. This comment used to say the opposite — that a
# translation of the previous wording stops being served — which was the first implementation,
# reverted after it ate 11 German strings on a real run. The old
# `if (!contains(key))` guard meant a key's English was captured once and never revisited,
# which a full rescan could not repair either.
add_decenza_test(tst_translationsourcedrift
    tst_translationsourcedrift.cpp
)
target_link_libraries(tst_translationsourcedrift PRIVATE Qt6::Qml)

# --- tst_markdownrenderer: markdown -> HTML, incl. the raw-HTML security claim ---
add_decenza_test(tst_markdownrenderer
    tst_markdownrenderer.cpp
)
target_link_libraries(tst_markdownrenderer PRIVATE Qt6::Qml)

# --- tst_emojiassets: the SHIPPED :/emoji resource set ---
# Compiles emoji.qrc in so this asserts what actually ships, not the source directory —
# without it a missing/stale qrc would silently strip every emoji in the app while
# tst_textescaping (which reads disk) stayed green.
add_decenza_test(tst_emojiassets
    tst_emojiassets.cpp
    ${CMAKE_SOURCE_DIR}/resources/emoji.qrc
)

# --- tst_textescaping: Theme.escapeHtml / replaceEmojiWithImg escaping contract ---
# Runs the real JS out of qml/Theme.qml in a QJSEngine, so it guards shipping source
# rather than a copy. Needs Qt6::Qml for QJSEngine.
add_decenza_test(tst_textescaping
    tst_textescaping.cpp
)
target_link_libraries(tst_textescaping PRIVATE Qt6::Qml)

# --- tst_recipesearch: Recipes-page tokenized search matcher ---
# Runs the real JS out of qml/components/RecipeSearch.js in a QJSEngine, so it guards
# shipping source rather than a copy. Needs Qt6::Qml for QJSEngine.
add_decenza_test(tst_recipesearch
    tst_recipesearch.cpp
)
target_link_libraries(tst_recipesearch PRIVATE Qt6::Qml)

# --- tst_beanbaseclient: Bean Base API client (debounce, rate limit, cache, parse) ---
add_decenza_test(tst_beanbaseclient
    tst_beanbaseclient.cpp
    ${CMAKE_SOURCE_DIR}/src/network/beanbaseclient.cpp
    ${CMAKE_SOURCE_DIR}/src/mcp/mcptools_beansearch.cpp
    ${CMAKE_SOURCE_DIR}/src/mcp/mcptoolregistry.h
)

# --- tst_autowakewindow: scheduled stay-awake window predicate (#1203) ---
add_decenza_test(tst_autowakewindow
    tst_autowakewindow.cpp
    ${CMAKE_SOURCE_DIR}/src/core/autowakemanager.cpp
)

# --- tst_dbtxn: DbWriteTxn lock discipline shared by the storages ---
# Header-only (dbutils.h), so no extra sources. Covers the outcomes the storage
# suites never reach: they all run uncontended and single-connection, so they
# only ever exercise the successful DbWriteTxn::begin() path.
add_decenza_test(tst_dbtxn
    tst_dbtxn.cpp
)

# --- tst_updatechecker: shared GitHub releases request + connection policy ---
# Guards ConnectionCacheExpiryTimeoutSecondsAttribute, whose absence is invisible
# except as an hourly Qt warning in shipped logs. Friend-class access is via
# `friend class tst_UpdateChecker` under DECENZA_TESTING in updatechecker.h.
add_decenza_test(tst_updatechecker
    tst_updatechecker.cpp
    ${CMAKE_SOURCE_DIR}/src/core/updatechecker.cpp
    ${CMAKE_BINARY_DIR}/version_code.cpp
)
target_include_directories(tst_updatechecker PRIVATE ${CMAKE_BINARY_DIR})

# --- tst_tclimport: TCL profile import round-trip against de1app profiles ---
add_decenza_test(tst_tclimport
    tst_tclimport.cpp
)
target_compile_definitions(tst_tclimport PRIVATE
    DE1APP_PROFILES_PATH="${CMAKE_CURRENT_SOURCE_DIR}/data/de1app_profiles"
)

# --- tst_recipegenerator: Frame generation vs de1app stock profiles ---
add_decenza_test(tst_recipegenerator
    tst_recipegenerator.cpp
)

# --- tst_shotanalysis: ShotAnalysis quality heuristics ---
add_decenza_test(tst_shotanalysis
    tst_shotanalysis.cpp
)

# --- tst_shotsummarizer: AI-prompt suppression cascade (issue #921) ---
# Pins the contract that ShotSummarizer's prompt path delegates detector
# orchestration to ShotAnalysis::generateSummary, so puck-failure shots
# can't leak misleading channeling/temp observations to the AI advisor.
# Qt::Quick + fastlinerenderer come in transitively via shotdatamodel.h —
# summarizeFromHistory() doesn't touch ShotDataModel itself, but the
# shotsummarizer.cpp translation unit links its symbols.
find_package(Qt6 REQUIRED COMPONENTS Quick Graphs)
# Compile the AI resource files (profile knowledge, dial-in reference,
# claude agent) into the test binary so ShotSummarizer's lazy loaders
# find them silently. Without this, calls to shotAnalysisSystemPrompt()
# emit qWarning lines that violate the "no warnings during test runs"
# policy in docs/CLAUDE_MD/TESTING.md §Test warnings.
qt_add_resources(TST_SHOTSUMMARIZER_AI_RESOURCES
    ${CMAKE_SOURCE_DIR}/resources/ai.qrc)
add_decenza_test(tst_shotsummarizer
    tst_shotsummarizer.cpp
    ${CMAKE_BINARY_DIR}/version_code.cpp
    ${TST_SHOTSUMMARIZER_AI_RESOURCES}
)
target_link_libraries(tst_shotsummarizer PRIVATE decenza_shotlib)
target_include_directories(tst_shotsummarizer PRIVATE ${CMAKE_BINARY_DIR})

# --- tst_aimanager: canonical-source separation end-to-end (task 10.5) ---
# Pins the contract that AIManager::emitRecentShotContext hoists profile
# intent + recipe + setup identity to a single header at the top of the
# "Previous Shots" section, and that legacy empty-string identity fields
# read as "inherit" rather than "different." Friend-class access is via
# `friend class tst_AIManager` under DECENZA_TESTING in aimanager.h.
qt_add_resources(TST_AIMANAGER_AI_RESOURCES
    ${CMAKE_SOURCE_DIR}/resources/ai.qrc)
add_decenza_test(tst_aimanager
    tst_aimanager.cpp
    ${CMAKE_SOURCE_DIR}/src/mcp/mcptools_ai_conversations.cpp
    ${CMAKE_SOURCE_DIR}/src/mcp/mcptoolregistry.h
    ${CMAKE_SOURCE_DIR}/src/ai/aimanager.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/aiprovider.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/aiconversation.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/dialing_blocks.cpp
    ${CMAKE_BINARY_DIR}/version_code.cpp
    ${TST_AIMANAGER_AI_RESOURCES}
)
target_link_libraries(tst_aimanager PRIVATE decenza_shotlib)
target_include_directories(tst_aimanager PRIVATE ${CMAKE_BINARY_DIR})

# --- tst_aiproviders: per-provider model catalog + setModel guard contract ---
# Pins availableModels()/setModel()/shortModelName()/modelName() for the
# user-selectable providers (OpenAI, Anthropic, Gemini): catalog order,
# constructor default = first entry, and the empty/unknown/valid setModel
# branches that AIManager drives from Settings.ai.providerModel(). Pure logic,
# so the target only needs aiprovider.cpp (no network/mocking).
add_decenza_test(tst_aiproviders
    tst_aiproviders.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/aiprovider.cpp
)

# --- tst_shotcorpus: Qt Test wrapper around shot_eval --validate.
# Runs the curated corpus under tests/data/shots/ as part of the regular
# Qt Test suite so Qt Creator's Tests panel picks it up alongside the
# other tst_* binaries. Without this wrapper, a bare add_test(NAME ...
# COMMAND ...) entry only surfaces in CTest, not in Qt Creator's Tests
# UI (which auto-discovers Q_OBJECT-derived test binaries). Mac
# developers running "Run all tests" in Qt Creator would otherwise miss
# this corpus check.
#
# The shot_eval target lives in the root CMakeLists.txt developer-tools
# block, which runs AFTER add_subdirectory(tests) — so TARGET shot_eval
# is not defined here yet. Use the same desktop-only gate that declares
# it, and pull the path through generator expressions at link time.
if(NOT ANDROID AND NOT IOS)
    add_executable(tst_shotcorpus tst_shotcorpus.cpp)
    target_link_libraries(tst_shotcorpus PRIVATE Qt6::Test Qt6::Core)
    target_compile_definitions(tst_shotcorpus PRIVATE
        TST_SHOTCORPUS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        TST_SHOTCORPUS_SHOT_EVAL="$<TARGET_FILE:shot_eval>"
    )
    add_dependencies(tst_shotcorpus shot_eval)
    add_test(NAME tst_shotcorpus COMMAND tst_shotcorpus)
    # Registered by hand because this target is declared with add_executable
    # rather than add_decenza_test, so the function that records test targets
    # never sees it. Omitting it made `ninja build_tests` skip this binary and
    # ctest report "40 - tst_shotcorpus (Not Run)" — caught only by building in
    # a CLEAN tree, since an incremental build still had the binary lying around
    # from a previous `ninja all`.
    #
    # This also pulls in shot_eval via add_dependencies above, which is correct:
    # the test shells out to `shot_eval --validate`, so that tool IS needed even
    # though the other three (saw_replay, saw_parity, profile_sync) are not.
    set_property(GLOBAL APPEND PROPERTY DECENZA_TEST_TARGETS tst_shotcorpus)
endif()

# --- tst_shotsettings: ShotSettings BLE wire format ---
add_decenza_test(tst_shotsettings
    tst_shotsettings.cpp
    mocks/MockTransport.h
)

# --- tst_mmrwrite: DE1Device::writeMMR per-register dedup (issue #783) ---
add_decenza_test(tst_mmrwrite
    tst_mmrwrite.cpp
    mocks/MockTransport.h
)

# --- tst_de1device_headless: GHC "headless" gate default + disconnect reset (#1470) ---
add_decenza_test(tst_de1device_headless
    tst_de1device_headless.cpp
    mocks/MockTransport.h
)

# --- tst_shotsampledecode: SetMixTemp/SetHeadTemp offsets in both BLE specs ---
add_decenza_test(tst_shotsampledecode
    tst_shotsampledecode.cpp
)

# --- tst_de1device_firmware: firmware-update BLE extensions on DE1Device ---
add_decenza_test(tst_de1device_firmware
    tst_de1device_firmware.cpp
    mocks/MockTransport.h
)

# --- tst_firmwareupdater: FirmwareUpdater state-machine tests ---
add_decenza_test(tst_firmwareupdater
    tst_firmwareupdater.cpp
    mocks/MockTransport.h
    ${CMAKE_SOURCE_DIR}/src/controllers/firmwareupdater.cpp
    ${CMAKE_SOURCE_DIR}/src/core/firmwareassetcache.cpp
)

# --- tst_profileupload: Profile upload frame-ACK verification ---
add_decenza_test(tst_profileupload
    tst_profileupload.cpp
    mocks/MockTransport.h
)

# --- tst_bleserviceerror: QLowEnergyService::ServiceError name mapping (#1586) ---
add_decenza_test(tst_bleserviceerror
    tst_bleserviceerror.cpp
)

# --- tst_blefidelity: BLE encode round-trip for all built-in profiles ---
add_decenza_test(tst_blefidelity
    tst_blefidelity.cpp
)
target_compile_definitions(tst_blefidelity PRIVATE
    SRCDIR="${CMAKE_CURRENT_SOURCE_DIR}"
    PROFILES_DIR="${CMAKE_SOURCE_DIR}/resources/profiles"
)

# ProfileManager sources (for MCP tool tests)
set(PROFILEMANAGER_SOURCES
    ${CMAKE_SOURCE_DIR}/src/controllers/profilemanager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/profilestorage.cpp
    ${CMAKE_SOURCE_DIR}/src/profile/profilesavehelper.cpp
)

# --- tst_coffeebags: coffee bag storage, preset->bag migration, transfer
# survival, and the unified bean search merge logic (bean-bag-inventory) ---
find_package(Qt6 REQUIRED COMPONENTS Quick)
add_decenza_test(tst_coffeebags
    tst_coffeebags.cpp
    ${CMAKE_SOURCE_DIR}/src/history/unifiedbeansearchmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/network/beanbaseclient.cpp
    ${CMAKE_BINARY_DIR}/version_code.cpp
)
target_link_libraries(tst_coffeebags PRIVATE decenza_shotlib)
target_include_directories(tst_coffeebags PRIVATE ${CMAKE_BINARY_DIR})

# --- tst_dbmigration: DB schema creation and migration chain ---
find_package(Qt6 REQUIRED COMPONENTS Quick)
add_decenza_test(tst_dbmigration
    tst_dbmigration.cpp
    ${CMAKE_BINARY_DIR}/version_code.cpp
)
target_link_libraries(tst_dbmigration PRIVATE decenza_shotlib)
target_include_directories(tst_dbmigration PRIVATE ${CMAKE_BINARY_DIR})

# --- tst_equipment: equipment package storage statics — grind/rpm split,
# rpmCapable derivation, package CRUD + identity dedup, migration-22 data step
# (add-equipment-packages). Static helpers only, so a minimal source set. ---
add_decenza_test(tst_equipment
    tst_equipment.cpp
    ${CMAKE_BINARY_DIR}/version_code.cpp
)
target_include_directories(tst_equipment PRIVATE ${CMAKE_BINARY_DIR})

# --- tst_recipestorage: recipe storage statics + lifecycle (add-recipes) —
# variant-map round-trip, CRUD, inventory MRU/shot-count aggregate,
# delete-vs-archive guard, clone provenance, bean-link open-bag resolution.
# CoffeeBagStorage rides along for the bag-resolution cases. ---
add_decenza_test(tst_recipestorage
    tst_recipestorage.cpp
    ${CMAKE_BINARY_DIR}/version_code.cpp
)
target_link_libraries(tst_recipestorage PRIVATE decenza_shotlib)
target_include_directories(tst_recipestorage PRIVATE ${CMAKE_BINARY_DIR})

# --- tst_recipepromotion: RecipePromotion::fieldsFromShotRecord — the shared
# promotion field-builder behind recipe_create_from_shot (MCP) and the
# recipes-idle-layout-upgrade starter-recipe hook. Header-only dependency
# (beanbase_blob.h's canonicalId is inline), no DB needed. ---
add_decenza_test(tst_recipepromotion
    tst_recipepromotion.cpp
    ${CMAKE_SOURCE_DIR}/src/history/recipepromotion.cpp
)

# --- tst_dialing_blocks: DB-backed coverage for the four shared
# block builders that drive both `dialing_get_context` (MCP) and the
# in-app advisor's user-prompt enrichment (issue #1044). Stands up a
# real ShotHistoryStorage schema, inserts curated fixtures, and asserts
# byte-equivalence between the two surfaces. Renamed from
# tst_mcptools_dialing_blocks alongside the src/mcp -> src/ai relocation
# in #1040.
# Compile the AI resource files (profile knowledge, dial-in reference,
# claude agent) into the test binary so ShotSummarizer's KB loaders
# find them silently during buildGrinderCalibrationBlock tests.
qt_add_resources(TST_DIALING_BLOCKS_AI_RESOURCES
    ${CMAKE_SOURCE_DIR}/resources/ai.qrc)
add_decenza_test(tst_dialing_blocks
    tst_dialing_blocks.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/dialing_blocks.cpp
    ${CMAKE_BINARY_DIR}/version_code.cpp
    ${TST_DIALING_BLOCKS_AI_RESOURCES}
)
target_link_libraries(tst_dialing_blocks PRIVATE decenza_shotlib)
target_include_directories(tst_dialing_blocks PRIVATE ${CMAKE_BINARY_DIR})
# Built-in profile corpus, for the KB-coverage gate (every shipped profile
# must resolve to a KB entry — restructure-kb-as-validated-json, task 5.2/6.6).
# SHOTS_CORPUS_DIR: the tests/data/shots/ regression corpus, for the
# companion KB-coverage gate over every shot fixture's profile_title
# (task 5.2 supplement).
target_compile_definitions(tst_dialing_blocks PRIVATE
    PROFILES_DIR="${CMAKE_SOURCE_DIR}/resources/profiles"
    SHOTS_CORPUS_DIR="${CMAKE_SOURCE_DIR}/tests/data/shots")

# --- tst_shotrecord_cache: ShotRecord::cachedAnalysis dedup + fallback ---
add_decenza_test(tst_shotrecord_cache
    tst_shotrecord_cache.cpp
    ${CMAKE_BINARY_DIR}/version_code.cpp
)
target_link_libraries(tst_shotrecord_cache PRIVATE decenza_shotlib)
target_include_directories(tst_shotrecord_cache PRIVATE ${CMAKE_BINARY_DIR})

# --- tst_shotimportdedupe: importShotRecord dedupe by visualizer_id (the
# Visualizer-recovery sync-gap case) ---
add_decenza_test(tst_shotimportdedupe
    tst_shotimportdedupe.cpp
    ${CMAKE_BINARY_DIR}/version_code.cpp
)
target_link_libraries(tst_shotimportdedupe PRIVATE decenza_shotlib)
target_include_directories(tst_shotimportdedupe PRIVATE ${CMAKE_BINARY_DIR})

# --- tst_scaleprotocol: Scale BLE packet parsing (Decent, Bookoo) ---
add_decenza_test(tst_scaleprotocol
    tst_scaleprotocol.cpp
    ${CMAKE_SOURCE_DIR}/src/ble/transport/scalebletransport.h
    ${CMAKE_SOURCE_DIR}/src/ble/scales/decentscale.cpp
    ${CMAKE_SOURCE_DIR}/src/ble/scales/bookooscale.cpp
)

# --- tst_decentscalewifi: WiFi scale driver (WebSocket protocol) ---
add_decenza_test(tst_decentscalewifi
    tst_decentscalewifi.cpp
    ${CMAKE_SOURCE_DIR}/src/ble/scales/decentscalewifi.cpp
)
target_link_libraries(tst_decentscalewifi PRIVATE
    Qt6::WebSockets
    Qt6::WebSocketsPrivate
    Qt6::CorePrivate
)

# --- tst_wifiscalediscovery: mDNS probe service ---
add_decenza_test(tst_wifiscalediscovery
    tst_wifiscalediscovery.cpp
    ${CMAKE_SOURCE_DIR}/src/network/wifiscalediscovery.cpp
)

# --- tst_difluidr2: DiFluid R2 refractometer packet parsing and name matching ---
add_decenza_test(tst_difluidr2
    tst_difluidr2.cpp
    ${CMAKE_SOURCE_DIR}/src/ble/transport/scalebletransport.h
    ${CMAKE_SOURCE_DIR}/src/ble/refractometers/refractometerdevice.cpp
    ${CMAKE_SOURCE_DIR}/src/ble/refractometers/difluidr2.cpp
)

# --- tst_difluidr1: DiFluid R1 key derivation, AES decrypt, plaintext parsing (#1307) ---
add_decenza_test(tst_difluidr1
    tst_difluidr1.cpp
    ${CMAKE_SOURCE_DIR}/src/ble/transport/scalebletransport.h
    ${CMAKE_SOURCE_DIR}/src/ble/refractometers/refractometerdevice.cpp
    ${CMAKE_SOURCE_DIR}/src/ble/refractometers/difluidr1.cpp
    ${CMAKE_SOURCE_DIR}/src/ble/refractometers/aes128.cpp
)
# Crypto backend: OpenSSL on desktop+Android, CommonCrypto (in libSystem) on iOS.
# Match the Q_OS_IOS branch in aes128.cpp exactly — macOS desktop still uses OpenSSL.
if(IOS)
    # CommonCrypto is part of libSystem — no extra linkage needed.
else()
    find_package(OpenSSL REQUIRED)
    target_link_libraries(tst_difluidr1 PRIVATE OpenSSL::Crypto)
endif()

# --- tst_weightprocessor: WeightProcessor edge cases (LSLR, oscillation, per-frame) ---
add_decenza_test(tst_weightprocessor
    tst_weightprocessor.cpp
    ${CMAKE_SOURCE_DIR}/src/machine/weightprocessor.cpp
    ${CMAKE_SOURCE_DIR}/src/machine/stepexitarbiter.cpp
)

# --- tst_scalefeedliveness: stall detector driven by sample arrival, not value
# change — a constant weight must not trip a false stall (#1176/#1185) ---
add_decenza_test(tst_scalefeedliveness
    tst_scalefeedliveness.cpp
    mocks/MockScaleDevice.h
    ${CMAKE_SOURCE_DIR}/src/machine/weightprocessor.cpp
    ${CMAKE_SOURCE_DIR}/src/machine/stepexitarbiter.cpp
)

# --- tst_scaleblepriority: connection-priority backoff decision logic (#1093/#1176) ---
# BlePriorityDetector is header-only and Qt-free — no extra sources to link.
add_decenza_test(tst_scaleblepriority
    tst_scaleblepriority.cpp
)

# --- tst_scaleskiphighlatch: BLEManager::ScaleSkipHighLatch value-type
# invariant (#1093/#1176, D7) + D9 SettingsHardware persisted store. The
# struct is header-inline; settings_hardware.cpp is QtCore-only (QSettings). ---
add_decenza_test(tst_scaleskiphighlatch
    tst_scaleskiphighlatch.cpp
)

# --- tst_sawprediction: shared SAW expected-drip kernel (σ + Gaussian math) ---
add_decenza_test(tst_sawprediction
    tst_sawprediction.cpp
)

# --- tst_saw: WeightProcessor SAW tests (minimal deps) ---
add_decenza_test(tst_saw
    tst_saw.cpp
    ${CMAKE_SOURCE_DIR}/src/machine/weightprocessor.cpp
    ${CMAKE_SOURCE_DIR}/src/machine/stepexitarbiter.cpp
)

# --- tst_visualizermixgoal: temperature.mix_goal / mix present-vs-absent in upload JSON ---
add_decenza_test(tst_visualizermixgoal
    tst_visualizermixgoal.cpp
    ${CMAKE_BINARY_DIR}/version_code.cpp
)
target_link_libraries(tst_visualizermixgoal PRIVATE decenza_shotlib)
target_include_directories(tst_visualizermixgoal PRIVATE ${CMAKE_BINARY_DIR})

# --- tst_sampleblobseries: mix goal series round-trip + legacy blob without the key ---
add_decenza_test(tst_sampleblobseries
    tst_sampleblobseries.cpp
    ${CMAKE_BINARY_DIR}/version_code.cpp
)
target_link_libraries(tst_sampleblobseries PRIVATE decenza_shotlib)
target_include_directories(tst_sampleblobseries PRIVATE ${CMAKE_BINARY_DIR})

# --- tst_settling: SAW settling trimSettlingData(), m_sawSettling flag, shot save ordering ---
find_package(Qt6 REQUIRED COMPONENTS Graphs Quick)
add_decenza_test(tst_settling
    tst_settling.cpp
    mocks/MockScaleDevice.h
)
target_link_libraries(tst_settling PRIVATE decenza_shotlib)

# --- tst_sav: MachineState SAV tests ---
add_decenza_test(tst_sav
    tst_sav.cpp
    mocks/MockScaleDevice.h
)

# --- Shared Q_OBJECT headers for tests using MockTransport (listed as sources for AUTOMOC) ---
set(MCP_MOC_HEADERS
    mocks/MockTransport.h
    ${CMAKE_SOURCE_DIR}/src/mcp/mcptoolregistry.h
    ${CMAKE_SOURCE_DIR}/src/mcp/mcpresourceregistry.h
)

# --- tst_profilemanager: ProfileManager lifecycle, signals, BLE, MCP resources, QML bindings ---
find_package(Qt6 REQUIRED COMPONENTS Graphs Quick Qml)
add_decenza_test(tst_profilemanager
    tst_profilemanager.cpp
    ${MCP_MOC_HEADERS}
    ${PROFILEMANAGER_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/mcp/mcpresources.cpp
    ${CMAKE_SOURCE_DIR}/src/network/webdebuglogger.cpp
    ${CMAKE_SOURCE_DIR}/src/core/memorymonitor.cpp
    ${CMAKE_BINARY_DIR}/version_code.cpp
)
target_link_libraries(tst_profilemanager PRIVATE decenza_shotlib Qt6::Qml paho-mqtt3a-static)
target_include_directories(tst_profilemanager PRIVATE ${CMAKE_BINARY_DIR})
target_compile_definitions(tst_profilemanager PRIVATE SRCDIR="${CMAKE_SOURCE_DIR}/tests")
# Add built-in profiles QRC so isBuiltInFilename() works in tests
qt_add_resources(PROFILES_QRC ${CMAKE_SOURCE_DIR}/resources/profiles.qrc)
target_sources(tst_profilemanager PRIVATE ${PROFILES_QRC})
# Also needed for compareWithBuiltin test (QFile::exists(":/profiles/..."))
target_sources(tst_tclimport PRIVATE ${PROFILES_QRC})

# --- MCP tool tests (ProfileManager + MockTransport) ---

# --- tst_mcptools_profiles: Profile MCP tools, PR #561 BLE upload regression ---
find_package(Qt6 REQUIRED COMPONENTS Graphs Quick)
add_decenza_test(tst_mcptools_profiles
    tst_mcptools_profiles.cpp
    ${MCP_MOC_HEADERS}
    ${CMAKE_SOURCE_DIR}/src/mcp/mcptools_profiles.cpp
    ${PROFILEMANAGER_SOURCES}
    ${CMAKE_BINARY_DIR}/version_code.cpp
)
target_link_libraries(tst_mcptools_profiles PRIVATE Qt6::Graphs Qt6::Quick paho-mqtt3a-static)
target_include_directories(tst_mcptools_profiles PRIVATE ${CMAKE_BINARY_DIR})
# Built-in profiles QRC, as tst_profilemanager and tst_tclimport already do.
# Without it ProfileManager cannot open ":/profiles/default.json" and falls
# back to whatever a PREVIOUS test left on disk — so this passed on a developer
# machine and on CI retry, and failed on the first attempt of every clean run.
# See the note on the ctest retry in linux-release.yml.
target_sources(tst_mcptools_profiles PRIVATE ${PROFILES_QRC})

# --- tst_mcptools_write: Write MCP tools (settings_set temp/weight BLE upload) ---
# mcptools_write.cpp includes all write tools, pulling in Accessibility/Screensaver/Translation/Battery,
# and BeanBaseClient since bag_update re-resolves the bag photo when the product URL changes.
find_package(Qt6 REQUIRED COMPONENTS TextToSpeech Multimedia)
add_decenza_test(tst_mcptools_write
    tst_mcptools_write.cpp
    ${MCP_MOC_HEADERS}
    ${CMAKE_SOURCE_DIR}/src/mcp/mcptools_write.cpp
    ${CMAKE_SOURCE_DIR}/src/network/beanbaseclient.cpp
    ${PROFILEMANAGER_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/core/accessibilitymanager.cpp
    ${CMAKE_SOURCE_DIR}/src/screensaver/screensavervideomanager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/batterymanager.cpp
    ${CMAKE_BINARY_DIR}/version_code.cpp
)
target_link_libraries(tst_mcptools_write PRIVATE decenza_shotlib Qt6::TextToSpeech Qt6::Multimedia paho-mqtt3a-static)
target_include_directories(tst_mcptools_write PRIVATE ${CMAKE_BINARY_DIR})
# Same missing built-in profiles QRC as tst_mcptools_profiles above.
target_sources(tst_mcptools_write PRIVATE ${PROFILES_QRC})

# --- tst_mcptools_presets: steam-pitcher / water-vessel preset MCP tools ---
# (unit conversions, partial-update merge, apply-on-select). MainController is
# passed null; CONTROLLER_SOURCES is linked only to satisfy its referenced symbols.
find_package(Qt6 REQUIRED COMPONENTS TextToSpeech Multimedia)
add_decenza_test(tst_mcptools_presets
    tst_mcptools_presets.cpp
    ${MCP_MOC_HEADERS}
    ${CMAKE_SOURCE_DIR}/src/mcp/mcptools_presets.cpp
    ${PROFILEMANAGER_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/core/accessibilitymanager.cpp
    ${CMAKE_SOURCE_DIR}/src/screensaver/screensavervideomanager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/batterymanager.cpp
    ${CMAKE_BINARY_DIR}/version_code.cpp
)
target_link_libraries(tst_mcptools_presets PRIVATE decenza_shotlib Qt6::TextToSpeech Qt6::Multimedia paho-mqtt3a-static)
target_include_directories(tst_mcptools_presets PRIVATE ${CMAKE_BINARY_DIR})

# --- tst_webdebuglogger: WebDebugLogger session-index cache (improve-mcp-debug-log-search) ---
add_decenza_test(tst_webdebuglogger
    tst_webdebuglogger.cpp
    ${CMAKE_SOURCE_DIR}/src/network/webdebuglogger.cpp
)

# --- tst_mcplogfilter: shared filter/tail helpers for debug_get_log / shots_get_debug_log ---
# (mcplogfilter.h is header-only — nothing to link beyond decenza_testlib's Qt Core.)
add_decenza_test(tst_mcplogfilter
    tst_mcplogfilter.cpp
)

# --- tst_mcptools_shots_debuglog: shots_get_debug_log filter/regex/tail (improve-mcp-debug-log-search) ---
add_decenza_test(tst_mcptools_shots_debuglog
    tst_mcptools_shots_debuglog.cpp
    ${MCP_MOC_HEADERS}
    ${CMAKE_SOURCE_DIR}/src/mcp/mcptools_shots.cpp
    ${PROFILEMANAGER_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/core/accessibilitymanager.cpp
    ${CMAKE_SOURCE_DIR}/src/screensaver/screensavervideomanager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/batterymanager.cpp
    ${CMAKE_BINARY_DIR}/version_code.cpp
)
target_link_libraries(tst_mcptools_shots_debuglog PRIVATE decenza_shotlib Qt6::TextToSpeech Qt6::Multimedia paho-mqtt3a-static)
target_include_directories(tst_mcptools_shots_debuglog PRIVATE ${CMAKE_BINARY_DIR})
# Same missing built-in profiles QRC as tst_mcptools_profiles above — McpTestFixture's
# ProfileManager falls back to "default" at construction and warns without it.
target_sources(tst_mcptools_shots_debuglog PRIVATE ${PROFILES_QRC})

# --- tst_mcpserver_session: Session management, ping, subscribe/unsubscribe ---
# Uses real mcpserver.cpp but stubs out tool/resource registration functions
# to avoid linking mcptools_*.cpp and their heavy transitive deps.
find_package(Qt6 REQUIRED COMPONENTS Graphs Quick)
add_decenza_test(tst_mcpserver_session
    tst_mcpserver_session.cpp
    ${MCP_MOC_HEADERS}
    ${CMAKE_SOURCE_DIR}/src/mcp/mcpserver.cpp
    ${CMAKE_SOURCE_DIR}/src/mcp/mcpserver.h
    ${CMAKE_SOURCE_DIR}/src/mcp/mcpsession.h
    ${PROFILEMANAGER_SOURCES}
    ${CMAKE_BINARY_DIR}/version_code.cpp
)
target_link_libraries(tst_mcpserver_session PRIVATE decenza_shotlib paho-mqtt3a-static)
target_include_directories(tst_mcpserver_session PRIVATE ${CMAKE_BINARY_DIR})

# --- tst_mcpserver_protocol: 2025-11-25 spec compliance (version negotiation,
# MCP-Protocol-Version header validation, Origin allowlist, structuredContent,
# resource_link extraction, title/icons/$schema). Stubs out tool/resource
# registration like tst_mcpserver_session — same dependency footprint.
find_package(Qt6 REQUIRED COMPONENTS Graphs Quick)
add_decenza_test(tst_mcpserver_protocol
    tst_mcpserver_protocol.cpp
    ${MCP_MOC_HEADERS}
    ${CMAKE_SOURCE_DIR}/src/mcp/mcpserver.cpp
    ${CMAKE_SOURCE_DIR}/src/mcp/mcpserver.h
    ${CMAKE_SOURCE_DIR}/src/mcp/mcpsession.h
    ${PROFILEMANAGER_SOURCES}
    ${CMAKE_BINARY_DIR}/version_code.cpp
)
target_link_libraries(tst_mcpserver_protocol PRIVATE decenza_shotlib paho-mqtt3a-static)
target_include_directories(tst_mcpserver_protocol PRIVATE ${CMAKE_BINARY_DIR})

# Compile the production qrc so :/icons/* paths resolve at runtime — the
# `icons[]` round-trip test reads SVGs out of the resource bundle and would
# fail with an empty array otherwise.
target_sources(tst_mcpserver_protocol PRIVATE ${CMAKE_SOURCE_DIR}/resources/resources.qrc)
set_target_properties(tst_mcpserver_protocol PROPERTIES AUTORCC ON)

# --- tst_mcpremoteaccess: remote connector (add-remote-mcp-connector) —
# capability-token lifecycle, dedicated-listener route gating, failed-token
# rate limiting, and end-to-end dispatch through the loopback listener. Same
# McpServer dependency footprint as tst_mcpserver_protocol, plus the remote
# coordinator. ---
add_decenza_test(tst_mcpremoteaccess
    tst_mcpremoteaccess.cpp
    ${MCP_MOC_HEADERS}
    ${CMAKE_SOURCE_DIR}/src/mcp/mcpserver.cpp
    ${CMAKE_SOURCE_DIR}/src/mcp/mcpserver.h
    ${CMAKE_SOURCE_DIR}/src/mcp/mcpsession.h
    ${CMAKE_SOURCE_DIR}/src/mcp/mcpremoteaccess.cpp
    ${CMAKE_SOURCE_DIR}/src/mcp/mcpremoteaccess.h
    ${CMAKE_SOURCE_DIR}/src/mcp/mcptunnel_tsnet.cpp
    ${CMAKE_SOURCE_DIR}/src/mcp/mcptunnel_tsnet.h
    ${PROFILEMANAGER_SOURCES}
    ${CMAKE_BINARY_DIR}/version_code.cpp
)
target_link_libraries(tst_mcpremoteaccess PRIVATE decenza_shotlib paho-mqtt3a-static)
target_include_directories(tst_mcpremoteaccess PRIVATE ${CMAKE_BINARY_DIR})
target_sources(tst_mcpremoteaccess PRIVATE ${CMAKE_SOURCE_DIR}/resources/resources.qrc)
set_target_properties(tst_mcpremoteaccess PROPERTIES AUTORCC ON)

# --- tst_accessibility_announcements: announce() routing rule (platform vs TTS) ---
# AccessibilityManager pulls TextToSpeech (m_tts construction in initTts) and
# QQuickWindow (root-window lookup in dispatchPlatformAnnouncement). The test
# itself never builds a window — the FakeAccessibilityManager subclass overrides
# the dispatch virtuals so no real Qt accessibility / TTS state is touched.
find_package(Qt6 REQUIRED COMPONENTS TextToSpeech Multimedia Quick Network)
add_decenza_test(tst_accessibility_announcements
    tst_accessibility_announcements.cpp
    ${CMAKE_SOURCE_DIR}/src/core/accessibilitymanager.cpp
)
target_link_libraries(tst_accessibility_announcements PRIVATE
    Qt6::TextToSpeech Qt6::Multimedia Qt6::Quick Qt6::Network
)

# --- tst_steamhealth: SteamDataModel summaries and SteamHealthTracker trend detection ---
add_decenza_test(tst_steamhealth
    tst_steamhealth.cpp
    ${CMAKE_SOURCE_DIR}/src/models/steamdatamodel.cpp
    ${CMAKE_SOURCE_DIR}/src/machine/steamhealthtracker.cpp
)
target_link_libraries(tst_steamhealth PRIVATE decenza_shotlib)

# --- tst_exifdate: EXIF date scanning from JPEG bytes.
# Header-only and dependency-free, which is the point: the same logic inside
# ShotServer::extractImageDate was untestable without linking a web server, a
# storage backend, a DE1 device and an AI manager — so it was never tested, and
# a rename silently made the whole path dead code. Nothing to link here.
add_decenza_test(tst_exifdate
    tst_exifdate.cpp
)

# --- tst_kb_schema: build-time KB validator (tools/validate_kb.py) coverage
# (change: restructure-kb-as-validated-json, task 5.1). Shells out to the
# same Python interpreter the root CMakeLists.txt's `validate_kb` build gate
# uses (find_package(Python3) there defines Python3_EXECUTABLE in this
# subdirectory's scope since add_subdirectory(tests) runs after that block),
# so this test exercises the exact command CI/local builds run — not a
# reimplementation. Asserts the real shipped profile_knowledge.json passes,
# and that a representative set of deliberately-corrupted fixtures (written
# to a QTemporaryDir at runtime) fail with an error naming the offending
# entry/key, per the profile-knowledge-base spec's five validator scenarios.
# No Decenza sources needed — pure QProcess + QJsonDocument plumbing.
if(NOT Python3_Interpreter_FOUND)
    message(FATAL_ERROR
        "Python3 not found — tst_kb_schema requires the same interpreter "
        "the validate_kb build gate uses. Install Python 3.")
endif()
add_decenza_test(tst_kb_schema
    tst_kb_schema.cpp
)
target_compile_definitions(tst_kb_schema PRIVATE
    VALIDATE_KB_PYTHON3="${Python3_EXECUTABLE}"
    VALIDATE_KB_SCRIPT="${CMAKE_SOURCE_DIR}/tools/validate_kb.py"
    VALIDATE_KB_REAL_JSON="${CMAKE_SOURCE_DIR}/resources/ai/profile_knowledge.json"
)

# --- failonwarning_lint: guard that every QTEST_*MAIN test file arms
# QTest::failOnWarning() (see tests/lint_failonwarning.cmake and the Handling
# Warnings section of docs/CLAUDE_MD/TESTING.md). A pure cmake -P check — no
# build, no extra dependency — that re-globs on each run, so a newly-added
# test file that forgets the guard fails `ctest` without a reconfigure.
add_test(NAME failonwarning_lint
    COMMAND ${CMAKE_COMMAND}
        -DLINT_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/lint_failonwarning.cmake)

# --- Sanitizer canaries: proof that each sanitizer is armed, not merely asked
# for. A silently-unapplied sanitizer yields the same green suite as a clean
# codebase, so each canary commits a deliberate defect of its sanitizer's kind
# and run_sanitizer_canary.cmake fails the test unless it was reported.
# Registered only under the matching option, so ordinary builds never compile
# or run them. See the header comments in the two canary sources.
#
# The UBSan canary runs in BOTH modes, which it previously did not. It used to
# be skipped whenever UBSan was in its auto-enabled Debug (recovering) mode, on
# the reasoning that a recovering sanitizer lets the process exit 0 and the
# canary would "fail" while everything worked. True as far as it goes — but
# recovering mode is every developer's local Debug build, i.e. exactly the gate
# that runs most often, and it was the one configuration with no armed-ness
# proof at all. The fix is to check the right evidence for the mode: an abort
# where the build halts, and the diagnostic text alone where it recovers.
if(ENABLE_UBSAN)
    add_executable(sanitizer_canary sanitizer_canary.cpp)
    set_target_properties(sanitizer_canary PROPERTIES AUTOMOC OFF)
    if(_ubsan_recover)
        set(_ubsan_expect_abort OFF)
    else()
        set(_ubsan_expect_abort ON)
    endif()
    add_test(NAME sanitizer_canary
        COMMAND ${CMAKE_COMMAND}
            -DCANARY=$<TARGET_FILE:sanitizer_canary>
            -DSANITIZER=ubsan
            -DEXPECT_ABORT=${_ubsan_expect_abort}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_sanitizer_canary.cmake)
endif()

# ASan has no recovering mode for this defect class, so it always aborts.
if(ENABLE_ASAN)
    add_executable(asan_canary asan_canary.cpp)
    set_target_properties(asan_canary PROPERTIES AUTOMOC OFF)
    add_test(NAME asan_canary
        COMMAND ${CMAKE_COMMAND}
            -DCANARY=$<TARGET_FILE:asan_canary>
            -DSANITIZER=asan
            -DEXPECT_ABORT=ON
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_sanitizer_canary.cmake)
endif()

# --- build_tests: everything ctest needs, and nothing it does not.
#
# `ninja` with no target builds `all`, which for this project is dominated by
# things the test suite never executes. Measured on a nightly sanitizer build:
# 385 C++ objects for the Decenza app and 36 for the tools (shot_eval,
# saw_replay, saw_parity, profile_sync), against 318 for the tests themselves —
# so roughly 56% of the compilation, plus the app's link, is work the job
# throws away.
#
# Safe because no test target links or depends on the app: they link
# decenza_testlib / decenza_shotlib, which compile the src/ sources they need
# independently. Sanitizer coverage is unaffected — ASan and UBSan report on
# code that RUNS, and this job runs only the suite.
#
# What this gives up, stated plainly: the nightly no longer compiles
# app-exclusive code (main.cpp, QML glue) under sanitizer flags. That is not a
# real loss — those files are already compiled several times a day by the
# platform builds, and nothing here executed them.
#
# Use: `ninja build_tests` instead of `ninja`.
get_property(_decenza_test_targets GLOBAL PROPERTY DECENZA_TEST_TARGETS)
if(ENABLE_UBSAN)
    list(APPEND _decenza_test_targets sanitizer_canary)
endif()
if(ENABLE_ASAN)
    list(APPEND _decenza_test_targets asan_canary)
endif()
add_custom_target(build_tests DEPENDS ${_decenza_test_targets})
list(LENGTH _decenza_test_targets _decenza_test_count)
message(STATUS "build_tests target covers ${_decenza_test_count} test executables")
