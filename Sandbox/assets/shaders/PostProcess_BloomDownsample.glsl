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

uniform sampler2D u_SrcTexture;
uniform vec2 u_SrcTexelSize;
uniform int u_MipLevel;
uniform float u_Threshold;
uniform float u_Knee;

float luma(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

float karisWeight(vec3 c) {
    return 1.0 / (1.0 + max(luma(c), 0.0));
}

vec3 softKneePrefilter(vec3 c) {
    c = max(c, vec3(0.0));
    float brightness = max(c.r, max(c.g, c.b));
    if (brightness <= 1e-6) {
        return vec3(0.0);
    }
    float rq = brightness - u_Threshold;
    if (u_Knee > 1e-5) {
        float soft = clamp(brightness - u_Threshold + u_Knee, 0.0, 2.0 * u_Knee);
        soft = (soft * soft) / (4.0 * u_Knee);
        rq = max(soft, brightness - u_Threshold);
    } else {
        rq = max(0.0, brightness - u_Threshold);
    }
    return c * (rq / brightness);
}

void main() {
    vec2 t = u_SrcTexelSize;

    vec3 a = texture(u_SrcTexture, v_UV + vec2(-2.0 * t.x,  2.0 * t.y)).rgb;
    vec3 b = texture(u_SrcTexture, v_UV + vec2( 0.0,        2.0 * t.y)).rgb;
    vec3 c = texture(u_SrcTexture, v_UV + vec2( 2.0 * t.x,  2.0 * t.y)).rgb;

    vec3 d = texture(u_SrcTexture, v_UV + vec2(-2.0 * t.x,  0.0)).rgb;
    vec3 e = texture(u_SrcTexture, v_UV).rgb;
    vec3 f = texture(u_SrcTexture, v_UV + vec2( 2.0 * t.x,  0.0)).rgb;

    vec3 g = texture(u_SrcTexture, v_UV + vec2(-2.0 * t.x, -2.0 * t.y)).rgb;
    vec3 h = texture(u_SrcTexture, v_UV + vec2( 0.0,       -2.0 * t.y)).rgb;
    vec3 i = texture(u_SrcTexture, v_UV + vec2( 2.0 * t.x, -2.0 * t.y)).rgb;

    vec3 j = texture(u_SrcTexture, v_UV + vec2(-1.0 * t.x,  1.0 * t.y)).rgb;
    vec3 k = texture(u_SrcTexture, v_UV + vec2( 1.0 * t.x,  1.0 * t.y)).rgb;
    vec3 l = texture(u_SrcTexture, v_UV + vec2(-1.0 * t.x, -1.0 * t.y)).rgb;
    vec3 m = texture(u_SrcTexture, v_UV + vec2( 1.0 * t.x, -1.0 * t.y)).rgb;

    vec3 g0 = (j + k + l + m) * 0.25;
    vec3 g1 = (a + b + d + e) * 0.25;
    vec3 g2 = (b + c + e + f) * 0.25;
    vec3 g3 = (d + e + g + h) * 0.25;
    vec3 g4 = (e + f + h + i) * 0.25;

    vec3 result;
    if (u_MipLevel == 0) {
        float w0 = 0.500 * karisWeight(g0);
        float w1 = 0.125 * karisWeight(g1);
        float w2 = 0.125 * karisWeight(g2);
        float w3 = 0.125 * karisWeight(g3);
        float w4 = 0.125 * karisWeight(g4);
        float wSum = max(w0 + w1 + w2 + w3 + w4, 1e-5);
        result = (g0 * w0 + g1 * w1 + g2 * w2 + g3 * w3 + g4 * w4) / wSum;
        result = softKneePrefilter(result);
    } else {
        result = g0 * 0.5 + (g1 + g2 + g3 + g4) * 0.125;
    }

    color = vec4(max(result, vec3(0.0)), 1.0);
}

