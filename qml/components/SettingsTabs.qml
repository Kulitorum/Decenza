pragma Singleton
import QtQuick
import Decenza

// Single source of truth for Settings tab order, ids, and sources.
// Adding or reordering a tab only requires editing the `tabs` list below.
QtObject {
    id: registry

    // Order here defines the visible tab order in SettingsPage.
    // id        — symbolic name used by navigation callers and the search index
    // key       — TranslationManager key for the localized tab label
    // fallback  — English fallback used when no translation is available
    // source    — QML source loaded into the tab's StackLayout page
    // loadSync  — if true, the tab is activated immediately when the page opens rather than on first visit (asynchronous: false is enforced globally in SettingsPage.qml)
    // debugOnly — if true, the tab is hidden unless Settings.app.isDebugBuild is set
    readonly property var tabs: [
        { id: "connections",    key: "settings.tab.connections",    fallback: "Connections",       source: "settings/SettingsConnectionsTab.qml",     loadSync: true,  debugOnly: false },
        { id: "machine",        key: "settings.tab.machine",        fallback: "Machine",           source: "settings/SettingsMachineTab.qml",         loadSync: false, debugOnly: false },
        { id: "calibration",    key: "settings.tab.calibration",    fallback: "Calibration",       source: "settings/SettingsCalibrationTab.qml",     loadSync: false, debugOnly: false },
        { id: "historyData",    key: "settings.tab.historyData",    fallback: "History & Data",    source: "settings/SettingsHistoryDataTab.qml",     loadSync: false, debugOnly: false },
        { id: "themes",         key: "settings.tab.themes",         fallback: "Themes",            source: "settings/SettingsThemesTab.qml",          loadSync: false, debugOnly: false },
        { id: "layout",         key: "settings.tab.layout",         fallback: "Layout",            source: "settings/SettingsLayoutTab.qml",          loadSync: false, debugOnly: false },
        { id: "screensaver",    key: "settings.tab.screensaver",    fallback: "Screensaver",       source: "settings/SettingsScreensaverTab.qml",     loadSync: false, debugOnly: false },
        { id: "visualizer",     key: "settings.tab.visualizer",     fallback: "Visualizer",        source: "settings/SettingsVisualizerTab.qml",      loadSync: false, debugOnly: false },
        { id: "ai",             key: "settings.tab.ai",             fallback: "AI",                source: "settings/SettingsAITab.qml",              loadSync: false, debugOnly: false },
        { id: "mqtt",           key: "settings.tab.mqtt",           fallback: "MQTT",              source: "settings/SettingsHomeAutomationTab.qml",  loadSync: false, debugOnly: false },
        { id: "languageAccess", key: "settings.tab.languageAccess", fallback: "Language & Access", source: "settings/SettingsLanguageTab.qml",        loadSync: false, debugOnly: false },
        { id: "about",          key: "settings.tab.about",          fallback: "About",             source: "settings/SettingsUpdateTab.qml",          loadSync: false, debugOnly: false },
        { id: "debug",          key: "settings.tab.debug",          fallback: "Debug",             source: "settings/SettingsDebugTab.qml",           loadSync: false, debugOnly: true  }
    ]

    // Map of id -> localized tab name. Rebuilt whenever the translation set
    // changes so that bindings using `SettingsTabs.tabLabels[id]` re-evaluate.
    readonly property var tabLabels: {
        // Read translationVersion to establish a binding dependency on the
        // current language — TranslationManager.translate() is a function and
        // would not create a dependency on its own.
        var _ = TranslationManager.translationVersion
        var out = {}
        for (var i = 0; i < tabs.length; i++) {
            out[tabs[i].id] = TranslationManager.translate(tabs[i].key, tabs[i].fallback)
        }
        return out
    }

    function visibleTabs() {
        var out = []
        for (var i = 0; i < tabs.length; i++) {
            var t = tabs[i]
            if (!t.debugOnly || Settings.app.isDebugBuild) out.push(t)
        }
        return out
    }

    // Returns the index into visibleTabs() for the given id, or -1 if unknown/hidden.
    function indexOf(id) {
        var vis = visibleTabs()
        for (var i = 0; i < vis.length; i++) {
            if (vis[i].id === id) return i
        }
        return -1
    }

    // Returns the localized name for a tab id, or "" if unknown.
    function tabName(id) {
        return tabLabels[id] || ""
    }

    // Ordered localized names for visible tabs (for the accessibility announcer).
    function visibleTabNames() {
        var vis = visibleTabs()
        var names = []
        for (var i = 0; i < vis.length; i++) {
            names.push(tabLabels[vis[i].id])
        }
        return names
    }
}
