#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
};

layout(binding = 1) uniform sampler2D source;    // coffee canvas (layer)
layout(binding = 2) uniform sampler2D maskTex;    // Mask.png: black = coffee, white = no coffee

void main() {
    vec4 coffee = texture(source, qt_TexCoord0);
    float mask = 1.0 - texture(maskTex, qt_TexCoord0).r; // invert: black->1 (show), white->0 (hide)
    fragColor = coffee * mask * qt_Opacity;
}
