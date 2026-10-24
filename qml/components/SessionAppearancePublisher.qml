import QtQuick
import Decenza

// Publishes native theme inputs, font-size roles, temperature-unit choice, and
// bounded background metadata used by the same-origin browser session. The
// browser uses the same Theme.qml with these inputs and its own viewport scale.
QtObject {
    readonly property var appearance: ({
        darkMode: Settings.theme.isDarkMode,
        fahrenheit: _fahrenheit,
        graph: Settings.graph.snapshot,
        fontSizes: Settings.theme.effectiveFontSizes,
        themeInputs: ({
            customThemeColors: Settings.theme.customThemeColors,
            backgroundPreset: Settings.theme.backgroundPreset,
            glassChrome: Settings.theme.glassChrome,
            flashColorName: Settings.theme.flashColorName,
            flashPhase: Settings.theme.flashPhase,
            bundledFontFamily: Settings.theme.bundledFontFamily,
            symbolFontFamily: Settings.theme.symbolFontFamily,
            activeShader: Settings.theme.activeShader === "crt" ? "crt" : "",
            shaderParams: Settings.theme.activeShader === "crt" ? Settings.theme.shaderParams : ({}),
            use12HourTime: Settings.app.use12HourTime,
            backgroundShotAdvanced: Settings.theme.backgroundShotAdvanced
        }),
        background: ({
            source: Settings.theme.backgroundSource,
            patternId: Settings.theme.activeBackgroundPattern.id || "",
            patternInk: String(Theme.contrastColorFor(Theme.backgroundColor)),
            imageRevision: MainController.shotServer
                          ? MainController.shotServer.sessionBackgroundImageRevision : ""
        })
    })

    // Keep the dependency explicit so the binding re-evaluates when the unit
    // preference changes, while taking the canonical conversion from Theme.
    readonly property bool _fahrenheit: {
        void(Settings.app.temperatureUnit)
        return Theme.tempIsFahrenheit()
    }

    function publish(): void {
        HostSessionService.publishAppearance(appearance)
        if (MainController.shotServer)
            MainController.shotServer.sessionAppearance = appearance
    }

    Component.onCompleted: publish()
    onAppearanceChanged: publish()
}
