#version 440

// The vehicle model, lit in world space so the shading stays put as the vehicle turns

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;

layout(location = 0) out vec4 vColor;

layout(std140, binding = 0) uniform Uniforms {
    mat4 model;
    mat4 modelViewProjection;
    vec4 lightDirection;
    vec4 baseColor;
} ubuf;

void main()
{
    vec3 n = normalize((ubuf.model * vec4(normal, 0.0)).xyz);
    float light = min(1.0, 0.3 + 0.7 * max(dot(n, ubuf.lightDirection.xyz), 0.0));
    vColor = vec4(ubuf.baseColor.rgb * light, ubuf.baseColor.a);
    gl_Position = ubuf.modelViewProjection * vec4(position, 1.0);
}
