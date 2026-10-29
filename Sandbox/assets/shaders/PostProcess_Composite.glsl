#type vertex
#version 410 core

out vec2 v_UV;

void main() {
    vec2 uv = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    v_UV = uv;
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}

#type fragment
#version 410 core

in vec2 v_UV;
out vec4 color;

uniform sampler2D u_SceneHDR;
uniform sampler2D u_BloomTexture;

uniform int u_BloomEnabled;
uniform float u_BloomIntensity;

uniform int u_ToneMapMode;
uniform float u_Exposure;

uniform float u_Temperature;
uniform float u_Tint;
uniform float u_Contrast;
uniform float u_Saturation;
uniform vec3 u_ShadowTint;
uniform vec3 u_HighlightTint;

uniform int u_VignetteEnabled;
uniform float u_VignetteIntensity;
uniform float u_VignetteSmoothness;
uniform vec3 u_VignetteColor;
uniform float u_AspectRatio;

vec3 toneMapACES(vec3 x) {
    vec3 num = x * (2.51 * x + vec3(0.03));
    vec3 den = x * (2.43 * x + vec3(0.59)) + vec3(0.14);
    return clamp(num / den, 0.0, 1.0);
}

vec3 toneMapStorybookNeutral(vec3 c) {
    const float startCompression = 0.8 - 0.04;
    const float desaturation = 0.15;

    float minCh = min(c.r, min(c.g, c.b));
    float offset = (minCh < 0.08) ? (minCh - 6.25 * minCh * minCh) : 0.04;
    c -= vec3(offset);

    float peak = max(c.r, max(c.g, c.b));
    if (peak < startCompression) {
        return clamp(c, 0.0, 1.0);
    }

    const float d = 1.0 - startCompression;
    float newPeak = 1.0 - (d * d) / (peak + d - startCompression);
    c *= (newPeak / peak);

    float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return clamp(mix(c, vec3(newPeak), g), 0.0, 1.0);
}

void main() {
    vec3 hdr = max(texture(u_SceneHDR, v_UV).rgb, vec3(0.0));
    if (u_BloomEnabled == 1) {
        vec3 bloom = max(texture(u_BloomTexture, v_UV).rgb, vec3(0.0));
        hdr += bloom * max(u_BloomIntensity, 0.0);
    }

    // Exposure (EV stops) & Pre-Tonemap Sensor White Balance
    hdr *= exp2(u_Exposure);
    vec3 wbGain = vec3(
        1.0 + 0.18 * u_Temperature + 0.08 * u_Tint,
        1.0 - 0.12 * u_Tint,
        1.0 - 0.18 * u_Temperature + 0.08 * u_Tint
    );
    hdr = max(hdr * max(wbGain, vec3(0.05)), vec3(0.0));

    // Tone Mapping to [0, 1] Display Range
    vec3 ldr = hdr;
    if (u_ToneMapMode == 0) {
        ldr = toneMapACES(hdr);
    } else if (u_ToneMapMode == 1) {
        ldr = toneMapStorybookNeutral(hdr);
    } else if (u_ToneMapMode == 2) {
        ldr = clamp(hdr / (vec3(1.0) + hdr), 0.0, 1.0);
    } else {
        ldr = clamp(hdr, 0.0, 1.0);
    }

    // Perceptual Split-Toning (Plum/Indigo Shadows + Warm Ivory Highlights)
    float baseLuma = dot(ldr, vec3(0.2126, 0.7152, 0.0722));
    float shadowMask = 1.0 - smoothstep(0.0, 0.55, baseLuma);
    float highlightMask = smoothstep(0.35, 1.0, baseLuma);
    vec3 splitGain = mix(vec3(1.0), max(u_ShadowTint, vec3(0.0)), shadowMask * 0.85) *
                     mix(vec3(1.0), max(u_HighlightTint, vec3(0.0)), highlightMask * 0.85);
    ldr *= splitGain;

    // Midpoint Contrast & Luma-Preserving Saturation
    ldr = (ldr - 0.5) * u_Contrast + 0.5;
    float gradedLuma = dot(ldr, vec3(0.2126, 0.7152, 0.0722));
    ldr = clamp(mix(vec3(gradedLuma), ldr, u_Saturation), 0.0, 1.0);

    // Colored Atmospheric Diorama Vignette
    if (u_VignetteEnabled == 1 && u_VignetteIntensity > 0.0001) {
        vec2 centered = (v_UV - 0.5) * 2.0;
        centered.x *= mix(1.0, max(u_AspectRatio, 1.0) * 0.82, 0.35);
        float dist = length(centered);
        float inner = max(0.05, 1.35 - u_VignetteIntensity * 0.95 - u_VignetteSmoothness * 0.55);
        float outer = inner + max(0.08, u_VignetteSmoothness * 1.15);
        float vigMask = smoothstep(inner, outer, dist) * clamp(u_VignetteIntensity * 1.25, 0.0, 1.0);
        ldr = mix(ldr, clamp(u_VignetteColor, 0.0, 1.0), clamp(vigMask, 0.0, 1.0));
    }

    color = vec4(ldr, 1.0);
}

