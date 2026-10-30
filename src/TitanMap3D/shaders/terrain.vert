#version 440

// Textured terrain with distance fog that fades into the sky's horizon colour

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 texCoord;

layout(location = 0) out vec2 vTexCoord;
layout(location = 1) out float vDistance;

layout(std140, binding = 0) uniform Uniforms {
    mat4 modelView;
    mat4 projection;
    vec4 fogColor;
    vec4 fogRange;  // start, end
} ubuf;

void main()
{
    vec4 viewPosition = ubuf.modelView * vec4(position, 1.0);
    vDistance = length(viewPosition.xyz);
    vTexCoord = texCoord;
    gl_Position = ubuf.projection * viewPosition;
}
