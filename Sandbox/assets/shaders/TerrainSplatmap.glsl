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

// 8-Layer Terrain Splatweight Textures (Slot 0: 0..3, Slot 1: 4..7)
uniform sampler2D u_SplatWeights0;
uniform sampler2D u_SplatWeights1;

// GL_TEXTURE_2D_ARRAY Samplers (Slot 2: Diffuse RGB + Height A, Slot 3: Tangent Normal RGB + Glossiness A)
uniform sampler2DArray u_AlbedoHeightArray;
uniform sampler2DArray u_NormalRoughnessArray;

// Shadow Maps
uniform sampler2D u_ShadowMap;
uniform sampler2D u_TorchShadowMap;

uniform float u_LayerTilingScales[8];
uniform float u_BlendSharpness;
uniform float u_EdgeDarkening;
uniform vec3 u_CameraPos;

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

uniform int u_TorchShadowsEnabled;
uniform vec4 u_TorchShadowPosRadius;
uniform mat4 u_TorchLightMatrices[4];
uniform vec2 u_TorchShadowTexelSize;
uniform float u_TorchShadowSoftness;
uniform float u_TorchShadowBias;

struct PointLightStd140 {
    vec4 PosRadius;      // xyz = world pos, w = radius
    vec4 ColorIntensity; // rgb = color, a = effective intensity
    vec4 Params;         // x = falloff, y = castsShadow (0/1), z = softness, w = bias
};

layout(std140) uniform PointLightBlock {
    ivec4 u_PointLightHeader; // x = active point light count [0..32], y = primary shadow light active
    PointLightStd140 u_PointLights[32];
};

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

void evaluatePointLights(vec3 worldPos,
                         vec3 normalWorld,
                         vec3 viewDir,
                         float glossiness,
                         out vec3 outDiffuse,
                         out vec3 outSpecular) {
    outDiffuse = vec3(0.0);
    outSpecular = vec3(0.0);

    int count = clamp(u_PointLightHeader.x, 0, 32);
    if (count == 0) {
        return;
    }

    float shininess = mix(8.0, 96.0, glossiness);

    for (int i = 0; i < count; ++i) {
        vec4 posRadius = u_PointLights[i].PosRadius;
        vec3 toLight = posRadius.xyz - worldPos;
        float distSq = dot(toLight, toLight);
        float radius = max(posRadius.w, 0.05);
        if (distSq < radius * radius) {
            float dist = sqrt(distSq);
            vec3 lightDir = toLight / max(dist, 1e-4);

            float x = clamp(1.0 - (dist / radius), 0.0, 1.0);
            float falloff = max(u_PointLights[i].Params.x, 0.25);
            float atten = pow(x * x * (3.0 - 2.0 * x), falloff);
            float torchVis = 1.0;

            if (i == 0 && u_TorchShadowsEnabled == 1 && u_PointLightHeader.y == 1) {
                vec3 toSample = worldPos - u_TorchShadowPosRadius.xyz;
                float horizDist = length(toSample.xz);
                if (horizDist >= 0.45) {
                    int quad = 0;
                    if (abs(toSample.x) > abs(toSample.z)) {
                        quad = (toSample.x >= 0.0) ? 1 : 3;
                    } else {
                        quad = (toSample.z >= 0.0) ? 2 : 0;
                    }
                    vec4 lightClip = u_TorchLightMatrices[quad] * vec4(worldPos, 1.0);
                    if (lightClip.w > 1e-5) {
                        vec3 projCoords = (lightClip.xyz / lightClip.w) * 0.5 + 0.5;
                        if (projCoords.z <= 1.0 && projCoords.x >= 0.0 && projCoords.x <= 1.0 &&
                            projCoords.y >= 0.0 && projCoords.y <= 1.0) {
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
                            float distRatio = clamp(horizDist / max(u_TorchShadowPosRadius.w, 1.0), 0.0, 1.0);
                            float softnessScale = mix(0.65, 2.6, distRatio) * max(u_TorchShadowSoftness, 0.25);
                            vec2 radiusUV = u_TorchShadowTexelSize * softnessScale;
                            float halfTexelU = u_TorchShadowTexelSize.x * 0.5;
                            float halfTexelV = u_TorchShadowTexelSize.y * 0.5;
                            float quadMinU = float(quad) * 0.25 + halfTexelU;
                            float quadMaxU = float(quad + 1) * 0.25 - halfTexelU;
                            float baseU = float(quad) * 0.25 + projCoords.x * 0.25;
                            float baseV = projCoords.y;
                            float bias = max(0.00015, u_TorchShadowBias * (1.0 - projCoords.z));
                            float currentDepth = projCoords.z - bias;
                            float litSum = 0.0;
                            for (int s = 0; s < 9; ++s) {
                                vec2 tapUV = vec2(
                                    clamp(baseU + poissonDisk[s].x * radiusUV.x, quadMinU, quadMaxU),
                                    clamp(baseV + poissonDisk[s].y * radiusUV.y, halfTexelV, 1.0 - halfTexelV)
                                );
                                float closestDepth = texture(u_TorchShadowMap, tapUV).r;
                                litSum += (currentDepth <= closestDepth) ? 1.0 : 0.0;
                            }
                            torchVis = litSum / 9.0;
                        }
                    }
                }
            }

            vec4 colInt = u_PointLights[i].ColorIntensity;
            vec3 lightIntensity = colInt.rgb * (colInt.a * atten * torchVis);

            // Subtle wrapped normal response for the 2.5D ground plane
            float nDotL = max(dot(normalWorld, lightDir), 0.0);
            float diffFactor = mix(1.0, nDotL, 0.35);
            outDiffuse += lightIntensity * diffFactor;

            // Half-vector Blinn-Phong specular highlight for Tar sheen and wet Cobblestone
            if (glossiness > 0.01) {
                vec3 halfVec = normalize(lightDir + viewDir);
                float nDotH = max(dot(normalWorld, halfVec), 0.0);
                float specFactor = pow(nDotH, shininess) * glossiness;
                outSpecular += lightIntensity * specFactor;
            }
        }
    }
}

void main() {
    vec2 worldXZ = v_WorldPos.xz;

    vec4 w0 = texture(u_SplatWeights0, v_SplatUV);
    vec4 w1 = texture(u_SplatWeights1, v_SplatUV);

    float rawWeights[8];
    rawWeights[0] = w0.r; rawWeights[1] = w0.g; rawWeights[2] = w0.b; rawWeights[3] = w0.a;
    rawWeights[4] = w1.r; rawWeights[5] = w1.g; rawWeights[6] = w1.b; rawWeights[7] = w1.a;

    float weightSum = 0.0;
    for (int i = 0; i < 8; ++i) {
        weightSum += rawWeights[i];
    }
    weightSum = max(weightSum, 1e-4);
    for (int i = 0; i < 8; ++i) {
        rawWeights[i] /= weightSum;
    }

    vec3 layerAlbedos[8];
    vec3 layerNormals[8];
    float layerHeights[8];
    float layerGlosses[8];

    float noiseVal = (u_BlendSharpness > 0.001) ? organicBreakup(worldXZ) : 0.5;

    // Fragment Fast-Path: Skip array texture fetches for inactive layers (< 0.004 weight)
    for (int i = 0; i < 8; ++i) {
        if (rawWeights[i] < 0.004) {
            layerAlbedos[i] = vec3(0.0);
            layerNormals[i] = vec3(0.0, 1.0, 0.0);
            layerHeights[i] = 0.5;
            layerGlosses[i] = 0.0;
            continue;
        }

        vec2 uv = worldXZ * u_LayerTilingScales[i];
        vec4 ah = texture(u_AlbedoHeightArray, vec3(uv, float(i)));
        vec4 nr = texture(u_NormalRoughnessArray, vec3(uv, float(i)));

        layerAlbedos[i] = ah.rgb;
        layerHeights[i] = mix(ah.a, (i % 2 == 0) ? noiseVal : (1.0 - noiseVal), 0.28);

        // Decode tangent normal (nx, ny, nz) where nz >= 0
        vec3 tn = nr.rgb * 2.0 - 1.0;
        // Map ground plane tangent space to world (X = +U, Z = +V, Y = +Up)
        layerNormals[i] = normalize(vec3(tn.x, max(tn.z, 0.05), -tn.y));
        layerGlosses[i] = nr.a;
    }

    // Height-modulated contrast blend
    float blendedWeights[8];
    for (int i = 0; i < 8; ++i) {
        blendedWeights[i] = rawWeights[i];
    }

    if (u_BlendSharpness > 0.001) {
        float scores[8];
        float maxScore = -100.0;
        for (int i = 0; i < 8; ++i) {
            if (rawWeights[i] < 0.004) {
                scores[i] = -1000.0;
                continue;
            }
            float modRamp = smoothstep(0.02, 0.28, rawWeights[i]);
            scores[i] = rawWeights[i] + layerHeights[i] * (u_BlendSharpness * 0.68) * modRamp;
            maxScore = max(maxScore, scores[i]);
        }

        float blendDepth = mix(0.90, 0.07, u_BlendSharpness);
        float threshold = maxScore - blendDepth;
        float bSum = 0.0;
        for (int i = 0; i < 8; ++i) {
            blendedWeights[i] = (rawWeights[i] >= 0.004) ? max(scores[i] - threshold, 0.0) : 0.0;
            bSum += blendedWeights[i];
        }
        bSum = max(bSum, 1e-5);
        for (int i = 0; i < 8; ++i) {
            blendedWeights[i] /= bSum;
        }
    }

    vec3 finalAlbedo = vec3(0.0);
    vec3 blendedNormal = vec3(0.0);
    float blendedGloss = 0.0;
    float dominant = 0.0;

    for (int i = 0; i < 8; ++i) {
        float bw = blendedWeights[i];
        if (rawWeights[i] >= 0.004 && bw > 0.0) {
            finalAlbedo += layerAlbedos[i] * bw;
            blendedNormal += layerNormals[i] * bw;
            blendedGloss += layerGlosses[i] * bw;
            dominant = max(dominant, bw);
        }
    }

    float nLen = length(blendedNormal);
    blendedNormal = (nLen > 1e-4) ? (blendedNormal / nLen) : vec3(0.0, 1.0, 0.0);

    // Edge ink transition darkening
    float transitionMask = smoothstep(0.04, 0.42, 1.0 - dominant);
    finalAlbedo *= (1.0 - u_EdgeDarkening * transitionMask);

    vec3 finalColor = finalAlbedo;

    // Apply 2.5D Directional Shadows + Cool Indigo Sky Fill + Flickering PointLights & Specular
    if (u_LightingEnabled == 1) {
        vec3 viewDir = normalize(u_CameraPos - v_WorldPos);
        float shadowVis = computeShadowVisibility(v_WorldPos);
        vec3 toSun = normalize(-u_SunDirection);
        float sunSlope = clamp(dot(blendedNormal, toSun) / max(toSun.y, 0.25), 0.45, 1.35);
        vec3 sunAndAmbient = u_AmbientColor * u_AmbientIntensity +
                             u_SunColor * (u_SunIntensity * shadowVis * sunSlope);

        vec3 ptDiffuse = vec3(0.0);
        vec3 ptSpecular = vec3(0.0);
        evaluatePointLights(v_WorldPos, blendedNormal, viewDir, blendedGloss, ptDiffuse, ptSpecular);

        finalColor = finalColor * (sunAndAmbient + ptDiffuse) + ptSpecular;
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
