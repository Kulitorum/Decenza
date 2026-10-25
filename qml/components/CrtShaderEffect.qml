import QtQuick
import Decenza

// GPU-accelerated CRT shader effect.
// Applied as layer.effect on the pageStack when Settings.theme.activeShader === "crt".
// Parameters use the shared settings and editor defaults.

ShaderEffect {
    // Uniforms: time and resolution
    // No initialiser: `NumberAnimation on time` below is a value source WITH AN EXPLICIT `from`,
    // so it starts there and the initialiser is discarded immediately. (A value source with
    // no `from` DOES start from the property's current value — qquickanimation.cpp:1324.
    // The rule is about the explicit `from`, not about value sources in general.) The `: 0` was dead. Removed
    // rather than honoured, so the shader timing is unchanged.
    property real time
    property real resWidth: width > 0 ? width : 960
    property real resHeight: height > 0 ? height : 600

    // Shader parameters bound to Settings (with defaults)
    property var _p: Settings.theme.shaderParams
    readonly property var defaults: ThemeManagementService.shaderDefaults
    property real scanlineIntensity: _p.scanlineIntensity !== undefined ? _p.scanlineIntensity : defaults.scanlineIntensity
    property real scanlineSize:      _p.scanlineSize      !== undefined ? _p.scanlineSize      : defaults.scanlineSize
    property real noiseIntensity:    _p.noiseIntensity    !== undefined ? _p.noiseIntensity    : defaults.noiseIntensity
    property real bloomStrength:     _p.bloomStrength     !== undefined ? _p.bloomStrength     : defaults.bloomStrength
    property real aberration:        _p.aberration        !== undefined ? _p.aberration        : defaults.aberration
    property real jitterAmount:      _p.jitterAmount      !== undefined ? _p.jitterAmount      : defaults.jitterAmount
    property real vignetteStrength:  _p.vignetteStrength  !== undefined ? _p.vignetteStrength  : defaults.vignetteStrength
    property real tintStrength:      _p.tintStrength      !== undefined ? _p.tintStrength      : defaults.tintStrength
    property real flickerAmount:     _p.flickerAmount     !== undefined ? _p.flickerAmount     : defaults.flickerAmount
    property real glitchRate:        _p.glitchRate        !== undefined ? _p.glitchRate        : defaults.glitchRate
    property real glowStart:         _p.glowStart         !== undefined ? _p.glowStart         : defaults.glowStart
    property real noiseSize:         _p.noiseSize         !== undefined ? _p.noiseSize         : defaults.noiseSize
    property real reflectionStrength: _p.reflectionStrength !== undefined ? _p.reflectionStrength : defaults.reflectionStrength
    // Color grading (applied last)
    property real colorGain:       _p.colorGain       !== undefined ? _p.colorGain       : defaults.colorGain
    property real colorContrast:   _p.colorContrast   !== undefined ? _p.colorContrast   : defaults.colorContrast
    property real colorSaturation: _p.colorSaturation !== undefined ? _p.colorSaturation : defaults.colorSaturation
    property real hueShift:        _p.hueShift        !== undefined ? _p.hueShift        : defaults.hueShift

    NumberAnimation on time {
        from: 0
        to: 10000
        duration: 10000000
        loops: Animation.Infinite
    }

    fragmentShader: "qrc:/shaders/crt.frag.qsb"
}
