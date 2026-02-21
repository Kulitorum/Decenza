#pragma once

// Shader system JavaScript: controls for device-side screen effects
// Toggles send settings to the device via API; the QML app renders the effect.
// Used by theme_page.h

inline constexpr const char* WEB_JS_SHADERS = R"JS(
// -- Shader registry --
var shaderRegistry = [
    { id: 'crt', name: 'CRT / Pip-Boy', desc: 'Scanlines, noise, bloom, jitter, and vignette' }
];

var deviceActiveShader = '';
var deviceShaderParams = {};

// -- Parameter definitions for CRT shader --
var shaderParamDefs = [
    { name: 'scanlineIntensity', label: 'Scanline Intensity', min: 0, max: 0.5,  step: 0.01,  def: 0.36 },
    { name: 'scanlineSize',      label: 'Scanline Size (px)', min: 1, max: 10,   step: 0.5,   def: 4.5  },
    { name: 'noiseIntensity',    label: 'Noise',              min: 0, max: 0.15, step: 0.005, def: 0.08 },
    { name: 'noiseSize',         label: 'Noise Detail',       min: 1, max: 10,   step: 0.5,   def: 3.5  },
    { name: 'bloomStrength',     label: 'Glow / Bloom',       min: 0, max: 0.8,  step: 0.02,  def: 0.52 },
    { name: 'glowStart',         label: 'Glow Start',         min: 0, max: 1,    step: 0.05,  def: 0    },
    { name: 'aberration',        label: 'Color Fringing',     min: 0, max: 4,    step: 0.1,   def: 0    },
    { name: 'jitterAmount',      label: 'Line Jitter',        min: 0, max: 3,    step: 0.1,   def: 1.4  },
    { name: 'vignetteStrength',  label: 'Vignette',           min: 0, max: 1.5,  step: 0.05,  def: 1.4  },
    { name: 'tintStrength',      label: 'Phosphor Tint',      min: 0, max: 1,    step: 0.01,  def: 1    },
    { name: 'flickerAmount',     label: 'Flicker',            min: 0, max: 0.2,  step: 0.005, def: 0.05 },
    { name: 'glitchRate',        label: 'Glitch Lines',       min: 0, max: 1,    step: 0.05,  def: 1    },
    { name: 'reflectionStrength',label: 'Glass Reflection',   min: 0, max: 0.3,  step: 0.01,  def: 0.22 }
];

function setDeviceShader(shaderId) {
    var newShader = (deviceActiveShader === shaderId) ? '' : shaderId;
    postJson('/api/theme/shader', { shader: newShader })
        .then(function(r) { return r.json(); })
        .then(function(data) {
            if (data.ok) {
                deviceActiveShader = data.shader;
                renderShaderPanel();
            }
        });
}

function setShaderParam(name, value) {
    value = parseFloat(value);
    deviceShaderParams[name] = value;
    // Update the displayed value
    var valSpan = document.getElementById('sp-val-' + name);
    if (valSpan) valSpan.textContent = value;
    // Send to device
    var body = {};
    body[name] = value;
    postJson('/api/theme/shader/params', body);
}

function resetShaderParams() {
    var body = {};
    for (var i = 0; i < shaderParamDefs.length; i++) {
        body[shaderParamDefs[i].name] = shaderParamDefs[i].def;
    }
    postJson('/api/theme/shader/params', body)
        .then(function(r) { return r.json(); })
        .then(function(data) {
            if (data.ok) {
                deviceShaderParams = body;
                renderShaderParams();
            }
        });
}

function renderShaderPanel() {
    var list = document.getElementById('shaderList');
    if (!list) return;
    list.innerHTML = '';
    for (var i = 0; i < shaderRegistry.length; i++) {
        var s = shaderRegistry[i];
        var on = deviceActiveShader === s.id;
        var item = document.createElement('div');
        item.className = 'shader-item';
        item.onclick = (function(id) { return function() { setDeviceShader(id); }; })(s.id);
        item.innerHTML =
            '<div class="shader-item-info">' +
                '<div class="shader-item-name">' + s.name + '</div>' +
                '<div class="shader-item-desc">' + s.desc + '</div>' +
            '</div>' +
            '<div class="shader-toggle' + (on ? ' on' : '') + '"></div>';
        list.appendChild(item);
    }
    renderShaderParams();
}

function renderShaderParams() {
    var container = document.getElementById('shaderParams');
    if (!container) return;
    var active = deviceActiveShader === 'crt';
    container.style.display = active ? 'block' : 'none';
    if (!active) return;

    container.innerHTML =
        '<div class="shader-params-header">' +
            '<span>CRT Settings</span>' +
            '<button class="shader-reset-btn" onclick="resetShaderParams()">Reset</button>' +
        '</div>';

    for (var i = 0; i < shaderParamDefs.length; i++) {
        var d = shaderParamDefs[i];
        var val = deviceShaderParams[d.name] !== undefined ? deviceShaderParams[d.name] : d.def;
        var row = document.createElement('div');
        row.className = 'shader-param-row';
        row.innerHTML =
            '<label class="shader-param-label">' + d.label + '</label>' +
            '<input type="range" class="shader-param-slider" ' +
                'min="' + d.min + '" max="' + d.max + '" step="' + d.step + '" ' +
                'value="' + val + '" ' +
                'oninput="setShaderParam(\'' + d.name + '\', this.value)">' +
            '<span class="shader-param-value" id="sp-val-' + d.name + '">' + val + '</span>';
        container.appendChild(row);
    }
}

// Initialize from theme data (called by renderAll)
function initShaderState(themeData) {
    if (themeData && themeData.screenEffect) {
        var se = themeData.screenEffect;
        if (typeof se.active === 'string') {
            deviceActiveShader = se.active;
        }
        // Load active effect's params from the effects map
        if (se.effects && se.effects[deviceActiveShader]) {
            deviceShaderParams = se.effects[deviceActiveShader];
        }
    }
    renderShaderPanel();
}
)JS";
