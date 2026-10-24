cmake_minimum_required(VERSION 3.21)

include("${CMAKE_SOURCE_DIR}/cmake/shared_remote_inputs.cmake")

find_package(Python3 COMPONENTS Interpreter REQUIRED)
# Stock Qt Remote Objects built for the same Wasm kit. The SDK is left untouched.
set(DECENZA_QTRO_PREFIX "${CMAKE_SOURCE_DIR}/build/qtro-wasm-install" CACHE PATH
    "Qt Remote Objects install prefix for the WebAssembly kit")
if(DECENZA_QTRO_PREFIX)
    list(PREPEND QT_ADDITIONAL_PACKAGES_PREFIX_PATH "${DECENZA_QTRO_PREFIX}")
    # This subdirectory runs after the kit toolchain. Apply the same cross-build
    # search roots Qt uses in Qt6/qt.toolchain.cmake:209-260.
    list(PREPEND CMAKE_PREFIX_PATH "${DECENZA_QTRO_PREFIX}/lib/cmake")
    list(PREPEND CMAKE_FIND_ROOT_PATH "${DECENZA_QTRO_PREFIX}")
endif()
set(DECENZA_QTMULTIMEDIA_PREFIX "${CMAKE_SOURCE_DIR}/build/qtmultimedia-wasm-install" CACHE PATH
    "Stock Qt Multimedia install prefix for the WebAssembly kit")
if(DECENZA_QTMULTIMEDIA_PREFIX)
    list(PREPEND QT_ADDITIONAL_PACKAGES_PREFIX_PATH "${DECENZA_QTMULTIMEDIA_PREFIX}")
    list(PREPEND CMAKE_PREFIX_PATH "${DECENZA_QTMULTIMEDIA_PREFIX}/lib/cmake")
    list(PREPEND CMAKE_FIND_ROOT_PATH "${DECENZA_QTMULTIMEDIA_PREFIX}")
endif()
find_package(Qt6 6.12.0 EXACT REQUIRED COMPONENTS Quick Qml Network QuickControls2 Graphs WebSockets QuickEffects RemoteObjects ShaderTools Multimedia Quick3D CanvasPainter)
# This stock module is installed beside the SDK. Qt6Qml was already loaded by
# the kit, so explicitly discover its additional static QML plugin.
find_package(Qt6quickmultimediaplugin 6.12.0 EXACT CONFIG REQUIRED
    PATHS "${DECENZA_QTMULTIMEDIA_PREFIX}/lib/cmake/Qt6Qml/QmlPlugins" NO_DEFAULT_PATH)


if(NOT TARGET Qt6::Platform)
    message(FATAL_ERROR "The selected Qt package does not expose Qt6::Platform; cannot verify the Wasm threading mode.")
endif()

get_target_property(_qt_platform_link_options Qt6::Platform INTERFACE_LINK_OPTIONS)
string(JOIN " " _qt_platform_link_options_text ${_qt_platform_link_options})
if(_qt_platform_link_options_text MATCHES "-pthread|PTHREADS|SHARED_MEMORY")
    message(FATAL_ERROR "This web client requires the Qt 6.12.0 wasm_singlethread kit; the selected Qt platform enables pthreads or shared memory.")
endif()

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(decenza-web
    main.cpp
)
# The shared application resources exceed Qt's 50 MB single-thread default.
set_target_properties(decenza-web PROPERTIES QT_WASM_INITIAL_MEMORY "64MB")
qt_add_shaders(decenza-web "crt_shaders"
    BATCHABLE
    PREFIX "/"
    BASE "${CMAKE_SOURCE_DIR}"
    FILES "${CMAKE_SOURCE_DIR}/shaders/crt.frag")

set(shared_qml_files
    StatusToast.qml BackgroundSurface.qml ThemedPageBackground.qml CurrentPageBackground.qml LastShotChartSource.qml LastShotChartRenderer.qml
    HistoryShotGraph.qml GraphSeries.qml GraphRightAxisLabels.qml PortalGraphOverlay.qml PortalGraphLiveSource.qml
    graphs/DecenzaGraphsTheme.qml graphs/GraphSeriesInstantiator.qml graphs/CustomLegend.qml SteamGraph.qml HoverTip.qml
    FastLineTraceLayer.qml EquipmentSummaryRenderer.qml EquipmentSummaryView.qml EquipmentSummaryFormat.js
    EquipmentInventoryView.qml EquipmentInventoryStyle.qml EquipmentCardView.qml EquipmentCardStyle.qml
    SharedAccessibleButton.qml SharedAccessibleTapArea.qml SharedButtonInteraction.qml SharedButtonStyle.qml
    SharedCardTapArea.qml SharedFocusIndicator.qml SharedIconButton.qml
    NativeEquipmentInventoryStyle.qml EquipmentCard.qml PuckPrepLabels.qml NativeButtonStyle.qml NativeButtonInteraction.qml
    AccessibleLabel.qml CircularGauge.qml ConnectionIndicator.qml LiveAdjustButton.qml
    GlobalSupportDialogs.qml VisualizerDuplicateResolver.qml CrashReportDialog.qml De1CommunicationErrorDialog.qml ProfileRefusedDialog.qml McpConfirmDialog.qml
    NativeEquipmentDialogStyle.qml NativeEquipmentDialogText.qml NativeDialogStyle.qml
    NativeKeyboardInteraction.qml NativeFormControlStyle.qml NativeSwitchStyle.qml
    AccessibleButton.qml AccessibleMouseArea.qml AccessibleTapHandler.qml DecenzaDialog.qml
    LiveCoachingBanner.qml MilkCapture.qml HomePresetActions.qml SharedPresetDraft.qml BrewDialog.qml SteamLabels.qml StyledSwitch.qml ThemedIcon.qml
    Tr.qml ValueInput.qml OnDemandLoader.qml RepeaterDelegateItem.qml
    SharedAppNavigation.qml OperationNavigationPolicy.js BrowserRouteHistory.js CommunityBrowseNavigation.js EspressoCompletionPolicy.js EspressoCompletionCoordinator.qml OperationCompletionOverlay.qml OperationCompletionState.qml
    BottomBar.qml NativeBottomBarStyle.qml SharedBottomBar.qml SharedBottomBarStyle.qml SharedSectionHeader.qml ThemeViewport.qml
    SharedDialogStyle.qml SharedDecenzaDialog.qml SharedSelectionDialog.qml
    SharedKeyboardInteraction.qml SharedKeyboardAwareContainer.qml SharedSuggestionFieldChrome.qml
    SharedSwitchStyle.qml SharedStyledSwitch.qml
    EquipmentDialogStyle.qml EquipmentDialogText.qml EquipmentUiBackend.qml SharedEquipmentUiBackend.qml
    SharedEquipmentDialog.qml SharedEquipmentDialogButton.qml SharedEquipmentSuggestionField.qml
    EquipmentInfoDialogView.qml
    ShotGraph.qml CupFillView.qml ExtractionViewSelector.qml PortalReadout.qml GraphLegend.qml PreferenceWrite.qml
    ShotGraphView.qml ShotGraphData.qml ShotGraphOptions.qml ShotGraphStyle.qml
    GraphRightAxisLabelsView.qml GraphEndMarkerArea.qml GraphUtils.js
    SharedTextField.qml FormControlStyle.qml SharedSuggestionField.qml RecipeNameEditor.qml
    RecipeDrinkCardView.qml RecipeDrinkCardStyle.qml RecipeCardText.js DrinkTypeCatalog.js ColoredIcon.qml
    SharedShotPlanView.qml SharedShotPlanStyle.qml SharedShotPlanFormat.js
    SharedBeanThumbnail.qml RecipeSearch.js DateUtils.js DrinkType.qml SharedBackgroundSurface.qml
    KeyboardAwareContainer.qml SuggestionField.qml SelectionDialog.qml CardTapArea.qml BeanThumbnail.qml FavoritesRowAction.qml
    AddLabel.qml BeanBaseDetailsPopup.qml BeanBaseSearchBar.qml ChangeBeansDialog.qml DatePickerDialog.qml
    EquipmentInfoDialog.qml EquipmentSummary.qml FavoritesListView.qml GrindField.qml GrindPickerDialog.qml
    GrindRowSource.qml GrindCalibrationSection.qml HideKeyboardButton.qml KbDerivedFromLabel.qml ProfileCard.qml ProfileDialInDiffBlock.qml
    ProfileFavoritesOrderDialog.qml ProfileGraph.qml ProfileInfoButton.qml ProfileKnowledgeDialog.qml ProfilePicker.qml
    ProfilePreviewPopup.qml RatioPresetDialog.qml RecipeDrinkCard.qml SearchField.qml ShotPlanText.qml
    SortControls.qml StyledComboBox.qml StyledIconButton.qml StyledTextField.qml SwitchEquipmentDialog.qml
    UnsavedChangesDialog.qml BagLifecycleLabels.qml ComparisonText.qml
    BeanBaseDetailsRow.qml
    BeanSummary.qml
    BagCard.qml
    BottomBarSubtitle.qml
    ComparisonGraph.qml
    ComparisonLineSwatch.qml
    ComparisonRow.qml
    ComparisonSectionHeader.qml
    ComparisonShotTable.qml
    LocalFileTransfer.qml LocalFileDownload.qml SystemLogDownload.qml LanguageStringEditor.qml LanguageFileActions.qml
    SettingsTabs.qml SettingsSearchEntries.js SettingsSearchMatcher.mjs
    BackgroundPickerDialog.qml StyledTabButton.qml
    FocusIndicator.qml PortalDevicePanel.qml QrCode.qml qrcode.js
    CrtOverlay.qml CrtShaderEffect.qml
    CommunitySuggestionsCard.qml BeanSuggestionsDialog.qml
    AIConversationPanel.qml
    ConversationOverlay.qml
    DecentUploadStatus.qml
    ExpandableTextArea.qml
    GraphChip.qml
    GraphChipRow.qml
    GraphFlowScaleCard.qml
    GraphOptionToggleCard.qml
    GraphOptionsButton.qml
    GraphOptionsDialog.qml
    GraphInspectBar.qml
    GraphReadout.qml
    HeaderPillButton.qml
    LinkBeanBaseDialog.qml
    PhaseSummaryPanel.qml
    ProfileEditorSection.qml
    QualityBadges.qml
    RatingInput.qml
    RecipeField.qml
    RecipeResolver.qml
    SearchKeywordChip.qml
    ShotAnalysisDialog.qml
    ShotResultsCard.qml
    SwipeableArea.qml
    TastePicker.qml
    library/LibraryPanel.qml library/LibraryItemCard.qml
    FlipClockScreensaver.qml StrangeAttractorScreensaver.qml ShotMapScreensaver.qml
    PipesScreensaver.qml ShotMapGlobe.qml
    ActionButton.qml SteamPlanText.qml
    DecentMachineChooser.qml TranslationErrorToast.qml UploadAccountSection.qml UploadDestinationCard.qml UploadMissingShots.qml ScrollDownIndicator.qml
    SubsystemLogView.qml SettingsActionRow.qml graphs/AutoRangingAxis.qml SettingsCard.qml SettingsSearchRegistry.qml ColorEditor.qml ColorSwatch.qml
    StatusBar.qml PortalStatusIndicator.qml PresetPillRow.qml PromptPulse.qml TempPickerDialog.qml
    layout/ShotPlanConfig.js layout/LayoutWrite.qml
    layout/LayoutWidgetItem.qml
    layout/LayoutItemDelegate.qml
    layout/LayoutBarZone.qml
    layout/LayoutCenterZone.qml
    layout/LayoutPreview.qml
    layout/items/EspressoItem.qml
    layout/items/SteamItem.qml
    layout/items/HotWaterItem.qml
    layout/items/FlushItem.qml
    layout/items/BeansItem.qml
    layout/items/RecipesItem.qml
    layout/items/EquipmentItem.qml
    layout/items/HistoryItem.qml
    layout/items/DiscussItem.qml
    layout/items/AutoFavoritesItem.qml
    layout/items/SleepItem.qml
    layout/items/SettingsItem.qml
    layout/items/TemperatureItem.qml
    layout/items/WaterLevelItem.qml
    layout/items/ScaleWeightItem.qml
    layout/items/ProfileNameItem.qml
    layout/items/DoseWeightItem.qml
    layout/items/MilkWeightItem.qml
    layout/items/RatioQuickSelectItem.qml
    layout/items/GrindQuickSelectItem.qml
    layout/items/TempQuickSelectItem.qml
    layout/items/ShotPlanItem.qml
    layout/items/SpacerItem.qml
    layout/LayoutActions.qml
    layout/GestureActionRow.qml
    layout/items/CustomItem.qml
    layout/items/WeatherItem.qml
    layout/items/PageTitleItem.qml
    layout/items/SteamTemperatureItem.qml
    layout/items/SeparatorItem.qml
    layout/items/QuitItem.qml
    layout/items/ScreensaverItem.qml
    layout/items/LastShotItem.qml
    layout/items/MachineStatusItem.qml
    layout/items/BatteryLevelItem.qml
    layout/items/ScaleBatteryItem.qml
    layout/items/MiniGHCItem.qml
    layout/items/ClockItem.qml
    layout/CustomEditorPopup.qml
    layout/ZoneOptionsPopup.qml
    layout/ReadoutOptionsPopup.qml
    layout/WidgetColor.qml
    layout/WidgetColorPicker.qml
    layout/SleepEditorPopup.qml
    layout/ScreensaverEditorPopup.qml
    layout/PillFit.js
    layout/EmojiPicker.qml
    layout/EmojiData.js
)
set(shared_qml_sources)
foreach(file IN LISTS shared_qml_files)
    set(source "${CMAKE_CURRENT_SOURCE_DIR}/../../qml/components/${file}")
    # Preserve the source tree layout so all shared relative JS imports resolve.
    set_source_files_properties("${source}" PROPERTIES QT_RESOURCE_ALIAS "components/${file}")
    list(APPEND shared_qml_sources "${source}")
endforeach()

foreach(singleton IN ITEMS SettingsTabs SettingsSearchRegistry HomePresetActions PuckPrepLabels LastShotChartSource GraphSeries DrinkType BagLifecycleLabels ComparisonText)
    set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/../../qml/components/${singleton}.qml"
        PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
endforeach()
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/../../qml/components/SteamLabels.qml"
    PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

foreach(singleton IN ITEMS LayoutActions WidgetColor)
    set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/../../qml/components/layout/${singleton}.qml"
        PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
endforeach()

set(_shared_theme "${CMAKE_CURRENT_SOURCE_DIR}/../../qml/Theme.qml")
set_source_files_properties("${_shared_theme}" PROPERTIES
    QT_RESOURCE_ALIAS "Theme.qml" QT_QML_SINGLETON_TYPE TRUE)
set(_shared_shell "${CMAKE_CURRENT_SOURCE_DIR}/../../qml/AppShell.qml")
set_source_files_properties("${_shared_shell}" PROPERTIES
    QT_RESOURCE_ALIAS "AppShell.qml" QT_QML_SINGLETON_TYPE TRUE)
set(_equipment_page "${CMAKE_CURRENT_SOURCE_DIR}/../../qml/pages/EquipmentPage.qml")
set_source_files_properties("${_equipment_page}" PROPERTIES QT_RESOURCE_ALIAS "pages/EquipmentPage.qml")
set(_recipes_page "${CMAKE_CURRENT_SOURCE_DIR}/../../qml/pages/RecipesPage.qml")
set_source_files_properties("${_recipes_page}" PROPERTIES QT_RESOURCE_ALIAS "pages/RecipesPage.qml")
set(_recipe_wizard_page "${CMAKE_CURRENT_SOURCE_DIR}/../../qml/pages/RecipeWizardPage.qml")
set_source_files_properties("${_recipe_wizard_page}" PROPERTIES QT_RESOURCE_ALIAS "pages/RecipeWizardPage.qml")
set(_steam_page "${CMAKE_CURRENT_SOURCE_DIR}/../../qml/pages/SteamPage.qml")
set_source_files_properties("${_steam_page}" PROPERTIES QT_RESOURCE_ALIAS "pages/SteamPage.qml")

set(shared_recipe_destination_sources)
foreach(page IN ITEMS settings/LayoutEditorZone SettingsPage settings/SettingsConnectionsTab settings/SettingsMachineTab settings/SettingsLayoutTab settings/SettingsSearchDialog ProfileImportPage VisualizerBrowserPage VisualizerMultiImportPage settings/SettingsHistoryDataTab settings/DeviceMigrationDialog settings/SettingsDebugTab AISettingsPage settings/SettingsAITab DescalingPage TransportPage settings/AddLanguagePage settings/StringBrowserPage settings/SettingsLanguageTab settings/SettingsVisualizerTab settings/SettingsHomeAutomationTab settings/SettingsUpdateTab settings/SettingsFirmwareTab settings/SettingsScreensaverTab settings/SettingsThemesTab settings/SettingsCalibrationTab SensorCalibrationPage FlowCalibrationPage ScreensaverPage CommunityBrowserPage IdlePage AutoFavoritesPage AutoFavoriteInfoPage ProfileSelectorPage EspressoPage HotWaterPage FlushPage BeanInfoPage ShotHistoryPage PostShotReviewPage ProfileInfoPage ProfileEditorPage SimpleProfileEditorPage FlowEditorPage PressureEditorPage DFlowEditorPage ShotComparisonPage)
    set(source "${CMAKE_CURRENT_SOURCE_DIR}/../../qml/pages/${page}.qml")
    set_source_files_properties("${source}" PROPERTIES QT_RESOURCE_ALIAS "pages/${page}.qml")
    list(APPEND shared_recipe_destination_sources "${source}")
endforeach()

# Developer-only diagnostics are kept outside the production Main.qml and are
# loaded only through the explicit ?diagnostics URL switch. Their fixture types
# stay in the optional tooling directory.
set(_live_qml_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../qml/webclient")
set(_tool_qml_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/webclient")
set(_tool_cpp_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/webclient")
foreach(file IN ITEMS Main.qml)
    set_source_files_properties("${_live_qml_dir}/${file}" PROPERTIES QT_RESOURCE_ALIAS "webclient/${file}")
endforeach()
foreach(file IN ITEMS FormProbe.qml RenderingProbe.qml DiagnosticsWindow.qml AccessibilityRepro.qml ListItemEditableChildRepro.qml)
    set_source_files_properties("${_tool_qml_dir}/${file}" PROPERTIES QT_RESOURCE_ALIAS "${file}")
endforeach()

set(_shared_ghc_window "${CMAKE_CURRENT_SOURCE_DIR}/../../qml/simulator/GHCSimulatorWindow.qml")
set_source_files_properties("${_shared_ghc_window}" PROPERTIES QT_RESOURCE_ALIAS "simulator/GHCSimulatorWindow.qml")

set(_shared_fuse "${CMAKE_CURRENT_SOURCE_DIR}/../../qml/third_party/fuse/fuse.mjs")
set_source_files_properties("${_shared_fuse}" PROPERTIES QT_RESOURCE_ALIAS "third_party/fuse/fuse.mjs")

qt_add_qml_module(decenza-web
    URI Decenza
    VERSION 1.0
    DEPENDENCIES QtQuick3D
    QML_FILES
        "${_shared_ghc_window}"
        "${_shared_fuse}"
        "${_shared_theme}"
        "${_shared_shell}"
        "${_equipment_page}"
        "${_steam_page}"
        "${_recipes_page}"
        "${_recipe_wizard_page}"
        "${_live_qml_dir}/Main.qml"
        "${_tool_qml_dir}/DiagnosticsWindow.qml"
        "${_tool_qml_dir}/RenderingProbe.qml"
        "${_tool_qml_dir}/FormProbe.qml"
        "${_tool_qml_dir}/AccessibilityRepro.qml"
        "${_tool_qml_dir}/ListItemEditableChildRepro.qml"
        ${shared_qml_sources}
        ${shared_recipe_destination_sources}
    SOURCES
        browserhistorycontroller.h
        browserhistorycontroller.cpp
        ../core/bundledfontregistration.h
        ../core/settingssearch.h
        ../core/settingssearch.cpp
        ../core/markdownrenderer.h
        ../core/markdownrenderer.cpp
        ../core/comboboxselection.h
        ../core/comboboxselection.cpp
        ../core/accessiblevaluechange.h
        ../core/accessiblevaluechange.cpp
        ${DECENZA_SHARED_REMOTE_SOURCES}
        ../core/coffeebagfields.h
        ../core/coffeebagfields.cpp
        ../profile/profile.cpp
        ../ble/protocol/binarycodec.cpp
        ../profile/profile.h
        ../profile/de1apptclfields.cpp
        ../profile/profileframe.cpp
        ../profile/profileparams.cpp
        ../profile/profilegenerator.cpp
        ../profile/profileanalyzer.cpp
        ../profile/profileeditmodel.h
        ../profile/profileeditmodel.cpp
        ../profile/profileeditordraft.h
        ../profile/profileeditordraft.cpp
        ../history/shotcomparisonreadout.cpp
        ../core/profilecataloglogic.h
        ../core/profilecataloglogic.cpp
        ../screensaver/strangeattractorrenderer.h
        ../screensaver/strangeattractorrenderer.cpp
        ../screensaver/pipegeometry.h
        ../screensaver/pipegeometry.cpp
        ../core/documentformatter.h
        ../core/documentformatter.cpp
        ../network/accountlink.h
        remoteobjectconnection.h
        remoteobjectconnection.cpp
        ../models/portalsampletools.h
        ../models/portalsampletools.cpp
        remoteaccessibilitymanager.h
        remoteaccessibilitymanager.cpp
        ../core/accessibilitytext.h
        remotetranslationmanager.h
        remotetranslationmanager.cpp
        ../core/translationcatalog.h
        ../core/translationcatalog.cpp
        remotesettings.h
        remotesettings.cpp
        remotewebdebuglogger.h
        remotewebdebuglogger.cpp
        remotemaincontroller.h
        remotemaincontroller.cpp
        ../core/preparationstate.h
        ../core/preparationstate.cpp
        ../core/steamcalculations.h
        ../network/sessionactionrequest.h
        ../network/sessionactionrequest.cpp
        ../network/steamhealthsnapshot.h
        ../network/steamhealthsnapshot.cpp
        ../network/steamuistate.h
        ../network/steamuistate.cpp
        ../core/emojiassets.h
        ../core/emojiassets.cpp
        ../profile/temperaturedisplay.h
        ../profile/temperaturedisplay.cpp
        ../network/machineuisnapshot.h
        ../network/machineuisnapshot.cpp
        ../network/steampitchersnapshot.h
        ../network/steampitchersnapshot.cpp
        ../machine/machinephase.h
        ../core/portalstate.h
        remotemachineuisource.h
        remotemachineuisource.cpp
        remotede1device.h
        remotede1device.cpp
        remoteprofilemanager.h
        remoteprofilemanager.cpp
        ../network/steameditrequest.h
        ../network/steameditrequest.cpp
        ../models/steamdatamodel.h
        ../models/steamdatamodel.cpp
        ../core/steamgraphvalidation.h
        remotescaledevice.h
        remotescaledevice.cpp
        remoteblemanager.h
        remoteblemanager.cpp
        remoteusbscalemanager.h
        remoteusbscalemanager.cpp
        remotemachinestate.h
        remotemachinestate.cpp
        remotebelkaportal.h
        remotebelkaportal.cpp
        remotesessionbackend.h
        remotesessionbackend.cpp
        ../core/backgroundpresets.cpp
        ../core/layoutcatalog.h
        ../core/layoutcatalog.cpp
        ../core/layoutsnapshot.cpp
        ../core/layoutsnapshot.h
        "${_tool_cpp_dir}/recordedtrace.h"
        "${_tool_cpp_dir}/recordedtrace.cpp"
        ../../src/core/popupcloser.h
        ../../src/core/popupcloser.cpp
        ../../src/core/keyboard.h
        ../../src/core/keyboard.cpp
        ../../src/core/localclipboard.h
        ../../src/core/localclipboard.cpp
        ../../src/rendering/fastlinerenderer.h
        ../../src/rendering/fastlinerenderer.cpp
)

include("${CMAKE_SOURCE_DIR}/cmake/remote_object_replicas.cmake")
foreach(_replica IN LISTS DECENZA_SHARED_REMOTE_REPLICA_NAMES)
    decenza_add_remote_object_replica(decenza-web ${_replica})
endforeach()

target_include_directories(decenza-web PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" ../remote ../network)
target_include_directories(decenza-web PRIVATE ../../src ../../src/core ../../src/profile ../../src/models ../../src/screensaver "${_tool_cpp_dir}")
target_include_directories(decenza-web PRIVATE "${DECENZA_PACKAGE_ID_INCLUDE_DIR}")
qt_add_resources(_shared_emoji_resources "../../resources/emoji.qrc")
target_sources(decenza-web PRIVATE ${_shared_emoji_resources})
qt_add_resources(decenza-web fixture
    PREFIX "/fixtures" BASE ../../tests/data/shots
    FILES ../../tests/data/shots/blooming_aborted.json)
include("${CMAKE_SOURCE_DIR}/cmake/cup_rendering.cmake")
decenza_add_cup_rendering(decenza-web)
qt_add_resources(decenza-web cup_images
    PREFIX "/CoffeeCup" BASE ../../resources/CoffeeCup
    FILES ../../resources/CoffeeCup/BackGround.png
          ../../resources/CoffeeCup/Mask.png
          ../../resources/CoffeeCup/Overlay.png)
qt_add_resources(decenza-web fonts
    PREFIX "/fonts" BASE ../../resources/fonts
    FILES
        ../../resources/fonts/DecenzaSans-Regular.ttf
        ../../resources/fonts/DecenzaSans-Medium.ttf
        ../../resources/fonts/DecenzaSans-Light.ttf
        ../../resources/fonts/DecenzaSans-Bold.ttf
        ../../resources/fonts/NotoSansMath-Regular.ttf)
qt_add_resources(decenza-web shared_ghc_image
    PREFIX "/" BASE ../../resources
    FILES ../../resources/GHC_Small.png)
qt_add_resources(decenza-web shared_qr_image
    PREFIX "/" BASE ../../Logo
    FILES ../../Logo/qrcode.png)
# Shared QML resolves icons by their canonical qrc:/icons/<filename> URLs. Keep
# the Wasm bundle in sync with the native icon catalogue instead of maintaining
# a page-by-page whitelist that silently misses icons as shared pages are added.
set(_icon_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../resources/icons")
file(GLOB _shared_icon_files CONFIGURE_DEPENDS "${_icon_dir}/*.svg")
set_source_files_properties(
    "${_icon_dir}/espresso 8mm.svg" PROPERTIES QT_RESOURCE_ALIAS "espresso.svg")
set_source_files_properties(
    "${_icon_dir}/flush 8mm.svg" PROPERTIES QT_RESOURCE_ALIAS "flush.svg")
qt_add_resources(decenza-web shared_icons
    PREFIX "/icons" BASE "${_icon_dir}"
    FILES ${_shared_icon_files})
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/../../resources/webclient/index.html"
    "${CMAKE_CURRENT_BINARY_DIR}/index.html" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/../../resources/webclient/qt-dialog-accessibility.js"
    "${CMAKE_CURRENT_BINARY_DIR}/qt-dialog-accessibility.js" COPYONLY)

target_link_libraries(decenza-web PRIVATE
    Qt6::Quick
    Qt6::CanvasPainter
    Qt6::Multimedia
    Qt6::Quick3D
    Qt6::Qml
    Qt6::Network
    Qt6::WebSockets
    Qt6::RemoteObjects
    Qt6::QuickControls2
    Qt6::Graphs
    Qt6::QuickEffects
)

target_compile_features(decenza-web PRIVATE cxx_std_17)

# Keep Qt's QML lint gate in the default web-client build.
if(TARGET decenza-web_qmllint)
    add_custom_target(webclient-qmllint ALL)
    add_dependencies(webclient-qmllint decenza-web_qmllint)
endif()

qt_add_resources(decenza-web backgrounds
    PREFIX "/backgrounds" BASE ../../resources/backgrounds
    FILES
        ../../resources/backgrounds/grain.svg
        ../../resources/backgrounds/dots.svg
        ../../resources/backgrounds/pinstripe.svg
        ../../resources/backgrounds/twill.svg
        ../../resources/backgrounds/weave.svg
        ../../resources/backgrounds/linen.svg)

# Stage the exact linked runtime together with its source-tree package ID.
# This target runs on every normal build, after linking, so changes to the
# separately configured HTML/accessibility bootstrap are packaged even when
# the C++ linker has no work to do.
set(_web_resource_sources "${CMAKE_CURRENT_BINARY_DIR}/decenza-web_resource_sources_$<CONFIG>.txt")
file(GENERATE OUTPUT "${_web_resource_sources}" CONTENT
    "source-root=${CMAKE_CURRENT_SOURCE_DIR}\nbuild-root=${CMAKE_CURRENT_BINARY_DIR}\n$<JOIN:$<TARGET_GENEX_EVAL:decenza-web,$<TARGET_PROPERTY:decenza-web,SOURCES>>,\n>\n")
set(_web_package_script "${CMAKE_SOURCE_DIR}/scripts/package_webclient.py")
set(_web_deployment_dir "${CMAKE_CURRENT_BINARY_DIR}/deployment")
add_custom_target(decenza-web-package ALL
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/webclient/check_webclient_resource_closure.py"
        --qml-manifest "${CMAKE_CURRENT_BINARY_DIR}/.qt/rcc/decenza-web_raw_qml_0.qrc"
        --resources-dir "${CMAKE_CURRENT_BINARY_DIR}/.qt/rcc"
        --target-sources "${_web_resource_sources}"
        --resource-qrc "${CMAKE_SOURCE_DIR}/resources/emoji.qrc"
    COMMAND "${Python3_EXECUTABLE}" "${_web_package_script}" pack
        --bundle-dir "$<TARGET_FILE_DIR:decenza-web>"
        --package-header "${DECENZA_PACKAGE_ID_HEADER}"
        --output-dir "${_web_deployment_dir}"
    COMMENT "Packaging the linked Decenza Wasm deployment"
    VERBATIM
)
add_dependencies(decenza-web-package decenza-web)


qt_add_resources(decenza-web map_textures
    PREFIX "/maps" BASE ../../resources/maps
    FILES ../../resources/maps/earth_night.png
          ../../resources/maps/earth_day.jpg
          ../../resources/maps/earth_bright.png)

target_link_libraries(decenza-web PRIVATE Qt6::CorePrivate)
