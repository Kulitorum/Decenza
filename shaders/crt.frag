#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float time;
    float resWidth;
    float resHeight;
    // Configurable parameters (bound from QML, synced via web UI)
    float scanlineIntensity;  // 0-0.5,  how dark scanlines get
    float scanlineSize;       // 1-10,   pixels per scanline period
    float noiseIntensity;     // 0-0.15, block noise strength
    float bloomStrength;      // 0-0.8,  glow/bloom amount
    float aberration;         // 0-4,    chromatic aberration px
    float jitterAmount;       // 0-3,    per-line horizontal jitter
    float vignetteStrength;   // 0-1.5,  edge darkening
    float tintStrength;       // 0-1,    phosphor green tint (1 = full monochrome)
    float flickerAmount;      // 0-0.03, brightness oscillation
    float glitchRate;         // 0-1,    glitch line frequency
    float glowStart;          // 0-1,    threshold for overexposure bloom-to-white
    float noiseSize;          // 1-10,   noise detail (1=coarse ~16px, 10=fine ~1.6px)
    float reflectionStrength; // 0-0.3,  CRT glass reflection highlight
};

layout(binding = 1) uniform sampler2D source;

// ── Precision-safe 2D hash (for jitter, glitch) ──
float hash21(vec2 p) {
    p = fract(p * vec2(443.8975, 397.2973));
    p += dot(p, p + 19.19);
    return fract(p.x * p.y);
}

// ── 3D hash returning pseudo-random gradient in [-1, 1] ──
vec3 hash33(vec3 p) {
    p = fract(p * vec3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yxz + 33.33);
    return -1.0 + 2.0 * fract(vec3(
        (p.x + p.y) * p.z,
        (p.x + p.z) * p.y,
        (p.y + p.z) * p.x
    ));
}

// ── 3D gradient noise (Perlin-style) ──
// 8 gradient lookups + trilinear interpolation, all ALU, no texture reads
float perlinNoise3D(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f);  // Hermite smoothstep

    return mix(
        mix(mix(dot(hash33(i + vec3(0, 0, 0)), f - vec3(0, 0, 0)),
                dot(hash33(i + vec3(1, 0, 0)), f - vec3(1, 0, 0)), u.x),
            mix(dot(hash33(i + vec3(0, 1, 0)), f - vec3(0, 1, 0)),
                dot(hash33(i + vec3(1, 1, 0)), f - vec3(1, 1, 0)), u.x), u.y),
        mix(mix(dot(hash33(i + vec3(0, 0, 1)), f - vec3(0, 0, 1)),
                dot(hash33(i + vec3(1, 0, 1)), f - vec3(1, 0, 1)), u.x),
            mix(dot(hash33(i + vec3(0, 1, 1)), f - vec3(0, 1, 1)),
                dot(hash33(i + vec3(1, 1, 1)), f - vec3(1, 1, 1)), u.x), u.y),
        u.z
    ) * 0.5 + 0.5;  // remap [-1,1] to [0,1]
}

void main() {
    vec2 res = vec2(resWidth, resHeight);
    vec2 uv = qt_TexCoord0;

    // ── Per-line horizontal jitter (CRT H-sync instability) ──
    float lineY = floor(uv.y * res.y);
    float tJitter = floor(mod(time * 12.0, 4096.0));
    float lineOffset = (hash21(vec2(lineY * 0.317, tJitter)) - 0.5)
                       * 2.0 * jitterAmount / res.x;
    uv.x += lineOffset;

    // ── Chromatic aberration: split R and B channels ──
    float aberrPx = aberration / res.x;
    vec3 color;
    color.r = texture(source, vec2(uv.x + aberrPx, uv.y)).r;
    color.g = texture(source, uv).g;
    color.b = texture(source, vec2(uv.x - aberrPx, uv.y)).b;

    // ── Scanlines ──
    float scanPeriod = max(scanlineSize, 1.0);
    float scan = sin(uv.y * res.y / scanPeriod * 6.28318) * 0.5 + 0.5;
    color *= 1.0 - scanlineIntensity * (1.0 - scan);

    // ── Phosphor green tint (before bloom: phosphor IS the color source on a CRT) ──
    float lum = dot(color, vec3(0.299, 0.587, 0.114));
    vec3 phosphor = vec3(lum * 0.15, lum, lum * 0.1);
    color = mix(color, phosphor, tintStrength);

    // ── Bloom: 4-tap glow when enabled ──
    if (bloomStrength > 0.01) {
        vec2 texel = 1.0 / res;
        float bs = scanPeriod;
        vec3 b = (texture(source, uv + vec2(bs, 0.0) * texel).rgb
                + texture(source, uv - vec2(bs, 0.0) * texel).rgb
                + texture(source, uv + vec2(0.0, bs) * texel).rgb
                + texture(source, uv - vec2(0.0, bs) * texel).rgb) * 0.25;
        float bLum = dot(b, vec3(0.299, 0.587, 0.114));
        color += b * smoothstep(0.25, 0.7, bLum) * bloomStrength;
    }
    // Self-bloom: boost bright pixels (free, no extra samples)
    float selfLum = dot(color, vec3(0.299, 0.587, 0.114));
    color += color * smoothstep(0.4, 0.9, selfLum) * bloomStrength * 0.4;

    // ── Overexposure: bright channels bleed toward white ──
    // Simulates phosphor saturation — a beam hitting hard enough makes all phosphors glow
    // glowStart is inverted: 0 = off (threshold 1.0), 1 = max (threshold 0.0)
    float peak = max(color.r, max(color.g, color.b));
    float gsThreshold = 1.0 - glowStart;
    float bleed = smoothstep(gsThreshold, gsThreshold + 0.3, peak);
    color = mix(color, vec3(peak), bleed);

    // ── CRT glass reflection: inverted-U highlight, top half only ──
    float vRefl = smoothstep(0.5, 0.0, uv.y);              // 1 at top, 0 at middle
    float hRefl = pow(abs(uv.x * 2.0 - 1.0), 0.7);        // U shape: edges bright, center dim
    color += vec3(vRefl * hRefl * reflectionStrength);

    // ── Vignette ──
    float vig = uv.x * uv.y * (1.0 - uv.x) * (1.0 - uv.y);
    vig = clamp(pow(16.0 * vig, 0.25), 0.0, 1.0);
    color *= mix(1.0, vig, vignetteStrength);

    // ── CRT static: 3D Perlin noise, Z = time for smooth organic animation ──
    // noiseSize scales XY: 1 = coarse (~16px cells), 10 = fine (~1.6px cells)
    float tNoise = mod(time * 8.0, 500.0);
    vec2 noiseUV = uv * res * 0.06 * noiseSize;
    float noise = perlinNoise3D(vec3(noiseUV, tNoise)) * 0.65
                + perlinNoise3D(vec3(noiseUV * 2.5 + 7.0, tNoise * 1.7 + 13.0)) * 0.35;
    color += (noise - 0.5) * noiseIntensity * 2.0;

    // ── Brightness flicker ──
    color *= 1.0 + sin(time * 4.7) * flickerAmount;

    // ── Horizontal glitch lines (one-frame, probability per line = glitchRate) ──
    // Two incommensurate time wraps give a long non-repeating cycle
    float gt1 = fract(time * 3.71);
    float gt2 = fract(time * 7.13);
    float bandHeight = scanPeriod / res.y;
    for (int g = 0; g < 3; g++) {
        float gf = float(g);
        float gRoll = hash21(vec2(gt1 + gf * 0.31, gt2 + gf * 0.53));
        if (gRoll < glitchRate) {
            float gY = hash21(vec2(gt1 + gf * 0.31 + 17.3, gt2 + gf * 0.53 + 43.7));
            float gBand = smoothstep(0.0, bandHeight, abs(uv.y - gY));
            color = mix(color + vec3(0.0, 0.06, 0.02), color, gBand);
        }
    }

    fragColor = vec4(color, 1.0) * qt_Opacity;
}
