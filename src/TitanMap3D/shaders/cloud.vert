#version 440

// A sheet of cloud: a horizontal quad in scene metres around the camera

layout(location = 0) in vec3 position;

layout(location = 0) out vec3 vPosition;

layout(std140, binding = 0) uniform Uniforms {
    mat4 modelViewProjection;
    vec4 camera;       // xyz
    vec4 scaleOffset;  // scale.xy, offset.xy
    vec4 detail;       // factor, offset.xy, weight
    vec4 weather;      // factor, offset.xy, strength
    vec4 shape;        // coverage, softness, opacity
    vec4 fade;         // fade start, fade end, fog start, fog end
    vec4 fogColor;
    vec4 litColor;
    vec4 shadeColor;
} ubuf;

void main()
{
    vPosition = position;
    gl_Position = ubuf.modelViewProjection * vec4(position, 1.0);
}
