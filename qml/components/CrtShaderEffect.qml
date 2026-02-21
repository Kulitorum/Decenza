import QtQuick

// GPU-accelerated CRT shader effect.
// Applied as layer.effect on the pageStack when Settings.activeShader === "crt".
// Parameters are read from Settings.shaderParams (set via web UI sliders).

ShaderEffect {
    // Uniforms: time and resolution
    property real time: 0
    property real resWidth: width > 0 ? width : 960
    property real resHeight: height > 0 ? height : 600

    // Shader parameters bound to Settings (with defaults)
    property var _p: Settings.shaderParams
    property real scanlineIntensity: _p.scanlineIntensity !== undefined ? _p.scanlineIntensity : 0.36
    property real scanlineSize:      _p.scanlineSize      !== undefined ? _p.scanlineSize      : 4.5
    property real noiseIntensity:    _p.noiseIntensity    !== undefined ? _p.noiseIntensity    : 0.08
    property real bloomStrength:     _p.bloomStrength     !== undefined ? _p.bloomStrength     : 0.52
    property real aberration:        _p.aberration        !== undefined ? _p.aberration        : 0.0
    property real jitterAmount:      _p.jitterAmount      !== undefined ? _p.jitterAmount      : 1.4
    property real vignetteStrength:  _p.vignetteStrength  !== undefined ? _p.vignetteStrength  : 1.4
    property real tintStrength:      _p.tintStrength      !== undefined ? _p.tintStrength      : 1.0
    property real flickerAmount:     _p.flickerAmount     !== undefined ? _p.flickerAmount     : 0.05
    property real glitchRate:        _p.glitchRate        !== undefined ? _p.glitchRate        : 1.0
    property real glowStart:         _p.glowStart         !== undefined ? _p.glowStart         : 0.0
    property real noiseSize:         _p.noiseSize         !== undefined ? _p.noiseSize         : 3.5
    property real reflectionStrength: _p.reflectionStrength !== undefined ? _p.reflectionStrength : 0.22

    NumberAnimation on time {
        from: 0
        to: 10000
        duration: 10000000
        loops: Animation.Infinite
    }

    fragmentShader: "qrc:/shaders/crt.frag.qsb"
}
