#type vertex
#version 410 core

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec4 a_Color;
layout(location = 2) in vec2 a_TexCoord;
layout(location = 3) in float a_TexIndex;
layout(location = 4) in float a_AlphaCutoff;
layout(location = 5) in float a_Emissive;
layout(location = 6) in float a_ShadowOffset;
layout(location = 7) in vec3 a_WindParams;

uniform mat4 u_ViewProjection;
uniform int u_WindActive;
uniform float u_WindTime;
uniform float u_WindStrength;
uniform float u_WindSpeed;
uniform float u_WindGustiness;
uniform float u_WindGustFreq;
uniform vec3 u_SwayRightAxis;

out vec3 v_WorldPos;
out vec4 v_Color;
out vec2 v_TexCoord;
flat out int v_TexIndex;
out float v_AlphaCutoff;
out float v_Emissive;
out float v_ShadowOffset;

void main() {
    vec3 worldPos = a_Position;
    if (a_WindParams.x > 0.0001 || abs(a_WindParams.z) > 0.0001) {
        float sway = 0.0;
        if (u_WindActive == 1 && a_WindParams.x > 0.0001) {
            float primary = sin(u_WindTime * u_WindSpeed + a_WindParams.y);
            float detail  = 0.32 * sin(u_WindTime * (u_WindSpeed * 2.17) + a_WindParams.y * 1.73 + 1.1);
            float gustOsc = 0.55 + 0.45 * sin(u_WindTime * u_WindGustFreq + a_WindParams.y * 0.37);
            float gustEnv = mix(1.0, gustOsc, u_WindGustiness);
            sway = (primary + detail) * u_WindStrength * gustEnv * a_WindParams.x;
        }
        worldPos += u_SwayRightAxis * (sway + a_WindParams.z);
    }

    v_WorldPos = worldPos;
    v_Color = a_Color;
    v_TexCoord = a_TexCoord;
    v_TexIndex = int(a_TexIndex + 0.5);
    v_AlphaCutoff = a_AlphaCutoff;
    v_Emissive = a_Emissive;
    v_ShadowOffset = a_ShadowOffset;
    gl_Position = u_ViewProjection * vec4(worldPos, 1.0);
}

#type fragment
#version 410 core

in vec3 v_WorldPos;
in vec4 v_Color;
in vec2 v_TexCoord;
flat in int v_TexIndex;
in float v_AlphaCutoff;
in float v_Emissive;
in float v_ShadowOffset;

out vec4 color;

uniform sampler2D u_Textures[13];
uniform sampler2D u_ShadowMap;
uniform sampler2D u_TorchShadowMap;

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

float computeShadowVisibility(vec3 worldPos, float receiverOffset) {
    if (u_ShadowsEnabled == 0) {
        return 1.0;
    }
    vec3 samplePos = worldPos - u_SunDirection * receiverOffset;
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

vec3 evaluatePointLights(vec3 worldPos, float receiverOffset) {
    vec3 accum = vec3(0.0);
    int count = clamp(u_PointLightHeader.x, 0, 32);
    for (int i = 0; i < count; ++i) {
        vec4 posRadius = u_PointLights[i].PosRadius;
        vec3 toLight = posRadius.xyz - worldPos;
        float distSq = dot(toLight, toLight);
        float radius = max(posRadius.w, 0.05);
        if (distSq < radius * radius) {
            float dist = sqrt(distSq);
            float x = clamp(1.0 - (dist / radius), 0.0, 1.0);
            float falloff = max(u_PointLights[i].Params.x, 0.25);
            float atten = pow(x * x * (3.0 - 2.0 * x), falloff);
            float torchVis = 1.0;
            if (i == 0 && u_TorchShadowsEnabled == 1 && u_PointLightHeader.y == 1) {
                vec3 toFrag = worldPos - u_TorchShadowPosRadius.xyz;
                float horizDist = length(toFrag.xz);
                if (horizDist >= 0.45) {
                    vec3 samplePos = worldPos;
                    if (receiverOffset > 0.001) {
                        vec2 dirToTorchXZ = normalize(-toFrag.xz);
                        float nudge = min(receiverOffset * 0.5, horizDist * 0.35);
                        samplePos.x += dirToTorchXZ.x * nudge;
                        samplePos.z += dirToTorchXZ.y * nudge;
                    }
                    vec3 toSample = samplePos - u_TorchShadowPosRadius.xyz;
                    int quad = 0;
                    if (abs(toSample.x) > abs(toSample.z)) {
                        quad = (toSample.x >= 0.0) ? 1 : 3;
                    } else {
                        quad = (toSample.z >= 0.0) ? 2 : 0;
                    }
                    vec4 lightClip = u_TorchLightMatrices[quad] * vec4(samplePos, 1.0);
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
            accum += colInt.rgb * (colInt.a * atten * torchVis);
        }
    }
    return accum;
}

void main() {
    vec4 texSample = vec4(1.0);
    switch (v_TexIndex) {
        case  0: texSample = texture(u_Textures[ 0], v_TexCoord); break;
        case  1: texSample = texture(u_Textures[ 1], v_TexCoord); break;
        case  2: texSample = texture(u_Textures[ 2], v_TexCoord); break;
        case  3: texSample = texture(u_Textures[ 3], v_TexCoord); break;
        case  4: texSample = texture(u_Textures[ 4], v_TexCoord); break;
        case  5: texSample = texture(u_Textures[ 5], v_TexCoord); break;
        case  6: texSample = texture(u_Textures[ 6], v_TexCoord); break;
        case  7: texSample = texture(u_Textures[ 7], v_TexCoord); break;
        case  8: texSample = texture(u_Textures[ 8], v_TexCoord); break;
        case  9: texSample = texture(u_Textures[ 9], v_TexCoord); break;
        case 10: texSample = texture(u_Textures[10], v_TexCoord); break;
        case 11: texSample = texture(u_Textures[11], v_TexCoord); break;
        case 12: texSample = texture(u_Textures[12], v_TexCoord); break;
    }
    vec4 texColor = v_Color * texSample;
    if (v_AlphaCutoff > 0.0 && texColor.a < v_AlphaCutoff) {
        discard;
    }

    vec3 finalRGB = texColor.rgb;
    if (u_LightingEnabled == 1 && v_Emissive >= 0.0) {
        float shadowVis = computeShadowVisibility(v_WorldPos, v_ShadowOffset);
        vec3 sunAndAmbient = u_AmbientColor * u_AmbientIntensity +
                             u_SunColor * (u_SunIntensity * shadowVis);
        vec3 pointGlow = evaluatePointLights(v_WorldPos, v_ShadowOffset);
        finalRGB = texColor.rgb * (sunAndAmbient + pointGlow + vec3(v_Emissive));
    }
    color = vec4(finalRGB, texColor.a);
}

