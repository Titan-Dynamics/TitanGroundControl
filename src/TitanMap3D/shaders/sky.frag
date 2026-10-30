#version 440

layout(location = 0) in vec3 vDirection;

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform Uniforms {
    vec4 topColor;
    vec4 horizonColor;
} ubuf;

void main()
{
    float elevation = normalize(vDirection).z;
    fragColor = mix(ubuf.horizonColor, ubuf.topColor, smoothstep(0.0, 0.6, elevation));
}
