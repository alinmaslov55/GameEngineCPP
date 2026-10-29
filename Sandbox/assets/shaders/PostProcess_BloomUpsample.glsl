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
uniform vec2 u_TexelRadius;

void main() {
    vec2 d = u_TexelRadius;
    vec3 s = vec3(0.0);
    s += texture(u_SrcTexture, v_UV + vec2(-d.x,  d.y)).rgb * 1.0;
    s += texture(u_SrcTexture, v_UV + vec2( 0.0,  d.y)).rgb * 2.0;
    s += texture(u_SrcTexture, v_UV + vec2( d.x,  d.y)).rgb * 1.0;

    s += texture(u_SrcTexture, v_UV + vec2(-d.x,  0.0)).rgb * 2.0;
    s += texture(u_SrcTexture, v_UV                   ).rgb * 4.0;
    s += texture(u_SrcTexture, v_UV + vec2( d.x,  0.0)).rgb * 2.0;

    s += texture(u_SrcTexture, v_UV + vec2(-d.x, -d.y)).rgb * 1.0;
    s += texture(u_SrcTexture, v_UV + vec2( 0.0, -d.y)).rgb * 2.0;
    s += texture(u_SrcTexture, v_UV + vec2( d.x, -d.y)).rgb * 1.0;

    color = vec4(max(s * (1.0 / 16.0), vec3(0.0)), 1.0);
}

