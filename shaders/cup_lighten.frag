#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
};

layout(binding = 1) uniform sampler2D source;      // underlying scene (layer)
layout(binding = 2) uniform sampler2D overlayTex;   // Overlay.png on black

void main() {
    vec4 base = texture(source, qt_TexCoord0);
    vec4 over = texture(overlayTex, qt_TexCoord0);
    // Lighten blend: max per channel
    vec3 rgb = max(base.rgb, over.rgb);
    // Derive alpha from brightness so black areas become transparent
    float brightness = max(rgb.r, max(rgb.g, rgb.b));
    float alpha = clamp(brightness * 4.0, 0.0, 1.0) * max(base.a, over.a);
    fragColor = vec4(rgb * alpha, alpha) * qt_Opacity;
}
