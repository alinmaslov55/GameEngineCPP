#type vertex
#version 410 core

layout(location = 0) in vec3 a_LocalPosition;
layout(location = 1) in vec2 a_SplatUV;

uniform mat4 u_ViewProjection;
uniform vec3 u_TerrainOrigin;
uniform vec2 u_TerrainSizeXZ;

out vec2 v_SplatUV;
out vec3 v_WorldPos;

void main() {
    vec3 worldPos = u_TerrainOrigin + vec3(
        a_LocalPosition.x * u_TerrainSizeXZ.x,
        0.0,
        a_LocalPosition.z * u_TerrainSizeXZ.y
    );
    v_SplatUV = a_SplatUV;
    v_WorldPos = worldPos;
    gl_Position = u_ViewProjection * vec4(worldPos, 1.0);
}

#type fragment
#version 410 core

layout(location = 0) out vec4 o_Color;

in vec2 v_SplatUV;
in vec3 v_WorldPos;

uniform sampler2D u_SplatMap;
uniform sampler2D u_Layer0;
uniform sampler2D u_Layer1;
uniform sampler2D u_Layer2;
uniform sampler2D u_Layer3;
uniform sampler2D u_ShadowMap;

uniform vec4 u_LayerTiling;
uniform float u_BlendSharpness;
uniform float u_EdgeDarkening;

// 2.5D Scene Lighting & Cutout Shadow Uniforms
uniform int u_LightingEnabled;
uniform vec3 u_SunDirection;
uniform vec3 u_SunColor;
uniform float u_SunIntensity;
uniform vec3 u_AmbientColor;
uniform float u_AmbientIntensity;

uniform int u_ShadowsEnabled;
uniform mat4 u_LightSpaceMatrix;
uniform float u_ShadowTexelSize;
uniform float u_ShadowSoftness;
uniform float u_ShadowBias;

uniform int u_PointLightCount;
uniform vec4 u_PointLightPosRadius[8];
uniform vec4 u_PointLightColorIntensity[8];
uniform float u_PointLightFalloff[8];

// Live 3D Editor Brush Ring Uniforms
uniform int u_BrushEnabled;
uniform vec2 u_BrushCenterXZ;
uniform float u_BrushRadius;
uniform float u_BrushFalloff;

float hash21(vec2 p) {
    p = fract(p * vec2(127.1, 311.7));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float valueNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(
        mix(hash21(i + vec2(0.0, 0.0)), hash21(i + vec2(1.0, 0.0)), u.x),
        mix(hash21(i + vec2(0.0, 1.0)), hash21(i + vec2(1.0, 1.0)), u.x),
        u.y
    );
}

float organicBreakup(vec2 xz) {
    return 0.65 * valueNoise(xz * 3.1) + 0.35 * valueNoise(xz * 7.3 + vec2(11.7, 5.3));
}

float computeShadowVisibility(vec3 worldPos) {
    if (u_ShadowsEnabled == 0) {
        return 1.0;
    }
    vec3 samplePos = worldPos - u_SunDirection * 0.015;
    vec4 lightClip = u_LightSpaceMatrix * vec4(samplePos, 1.0);
    vec3 projCoords = (lightClip.xyz / max(lightClip.w, 1e-5)) * 0.5 + 0.5;

    if (projCoords.z > 1.0 || projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0) {
        return 1.0;
    }

    const vec2 poissonDisk[9] = vec2[9](
        vec2( 0.000,  0.000),
        vec2(-0.707, -0.707),
        vec2( 0.707, -0.707),
        vec2(-0.707,  0.707),
        vec2( 0.707,  0.707),
        vec2(-1.000,  0.000),
        vec2( 1.000,  0.000),
        vec2( 0.000, -1.000),
        vec2( 0.000,  1.000)
    );

    float radiusUV = u_ShadowTexelSize * max(u_ShadowSoftness, 0.25);
    float currentDepth = projCoords.z - u_ShadowBias;
    float litSum = 0.0;
    for (int i = 0; i < 9; ++i) {
        float closestDepth = texture(u_ShadowMap, projCoords.xy + poissonDisk[i] * radiusUV).r;
        litSum += (currentDepth <= closestDepth) ? 1.0 : 0.0;
    }
    return litSum / 9.0;
}

vec3 evaluatePointLights(vec3 worldPos) {
    vec3 accum = vec3(0.0);
    for (int i = 0; i < u_PointLightCount; ++i) {
        vec3 lPos = u_PointLightPosRadius[i].xyz;
        float radius = max(u_PointLightPosRadius[i].w, 0.05);
        float dist = length(lPos - worldPos);
        if (dist < radius) {
            float x = clamp(1.0 - (dist / radius), 0.0, 1.0);
            float atten = pow(x * x * (3.0 - 2.0 * x), max(u_PointLightFalloff[i], 0.25));
            accum += u_PointLightColorIntensity[i].rgb * (u_PointLightColorIntensity[i].a * atten);
        }
    }
    return accum;
}

void main() {
    vec2 worldXZ = v_WorldPos.xz;
    vec4 w = texture(u_SplatMap, v_SplatUV);
    float wSum = max(w.r + w.g + w.b + w.a, 1e-4);
    w /= wSum;

    vec3 c0 = texture(u_Layer0, worldXZ * u_LayerTiling.x).rgb;
    vec3 c1 = texture(u_Layer1, worldXZ * u_LayerTiling.y).rgb;
    vec3 c2 = texture(u_Layer2, worldXZ * u_LayerTiling.z).rgb;
    vec3 c3 = texture(u_Layer3, worldXZ * u_LayerTiling.w).rgb;

    vec4 b = w;
    if (u_BlendSharpness > 0.001) {
        float n = organicBreakup(worldXZ);
        vec3 lumWeights = vec3(0.299, 0.587, 0.114);

        float h0 = mix(dot(c0, lumWeights), n, 0.42);
        float h1 = mix(dot(c1, lumWeights), 1.0 - n, 0.42);
        float h2 = mix(dot(c2, lumWeights), n, 0.28);
        float h3 = mix(dot(c3, lumWeights), 1.0 - n, 0.38);

        vec4 modRamp = smoothstep(vec4(0.02), vec4(0.28), w);
        vec4 scores = w + vec4(h0, h1, h2, h3) * (u_BlendSharpness * 0.68) * modRamp;

        float blendDepth = mix(0.90, 0.07, u_BlendSharpness);
        float maxScore = max(max(scores.x, scores.y), max(scores.z, scores.w)) - blendDepth;
        b = max(scores - vec4(maxScore), vec4(0.0));
        b /= max(b.r + b.g + b.b + b.a, 1e-5);
    }

    vec3 finalColor = c0 * b.r + c1 * b.g + c2 * b.b + c3 * b.a;

    float dominant = max(max(b.r, b.g), max(b.b, b.a));
    float transitionMask = smoothstep(0.04, 0.42, 1.0 - dominant);
    finalColor *= (1.0 - u_EdgeDarkening * transitionMask);

    // Apply 2.5D Directional Cutout Shadows + Cool Indigo Shade + Flickering PointLights
    if (u_LightingEnabled == 1) {
        float shadowVis = computeShadowVisibility(v_WorldPos);
        vec3 sunAndAmbient = u_AmbientColor * u_AmbientIntensity +
                             u_SunColor * (u_SunIntensity * shadowVis);
        vec3 pointGlow = evaluatePointLights(v_WorldPos);
        finalColor *= (sunAndAmbient + pointGlow);
    }

    // Live 3D ground brush cursor ring stays unlit on top for clear visibility at night
    if (u_BrushEnabled == 1) {
        float dist = length(worldXZ - u_BrushCenterXZ);
        float aaWidth = max(fwidth(dist) * 1.6, 0.02);

        float outerRing = 1.0 - smoothstep(0.0, aaWidth, abs(dist - u_BrushRadius));
        float innerRadius = u_BrushRadius * clamp(1.0 - u_BrushFalloff, 0.05, 0.98);
        float innerRing = (1.0 - smoothstep(0.0, aaWidth, abs(dist - innerRadius))) * 0.42;
        float insideTint = (1.0 - smoothstep(u_BrushRadius - aaWidth, u_BrushRadius + aaWidth, dist)) * 0.07;

        vec3 ringColor = vec3(0.98, 0.86, 0.36);
        float overlayAlpha = clamp(outerRing * 0.85 + innerRing + insideTint, 0.0, 0.9);
        finalColor = mix(finalColor, ringColor, overlayAlpha);
    }

    o_Color = vec4(finalColor, 1.0);
}

