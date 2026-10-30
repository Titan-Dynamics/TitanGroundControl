#version 440

// Textured quads: waypoint markers and their labels

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 texCoord;

layout(location = 0) out vec2 vTexCoord;

layout(std140, binding = 0) uniform Uniforms {
    mat4 modelViewProjection;
} ubuf;

void main()
{
    vTexCoord = texCoord;
    gl_Position = ubuf.modelViewProjection * vec4(position, 1.0);
}
