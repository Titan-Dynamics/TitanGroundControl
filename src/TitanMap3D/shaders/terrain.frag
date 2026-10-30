#version 440

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in float vDistance;

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform Uniforms {
    mat4 modelView;
    mat4 projection;
    vec4 fogColor;
    vec4 fogRange;
} ubuf;

layout(binding = 1) uniform sampler2D imagery;

void main()
{
    vec3 color = texture(imagery, vTexCoord).rgb;
    float fog = smoothstep(ubuf.fogRange.x, ubuf.fogRange.y, vDistance);
    fragColor = vec4(mix(color, ubuf.fogColor.rgb, fog), 1.0);
}
