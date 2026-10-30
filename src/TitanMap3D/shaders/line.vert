#version 440

// Lines of any width: every segment is a screen-aligned quad, expanded here from its two endpoints

layout(location = 0) in vec3 pointA;
layout(location = 1) in vec3 pointB;
layout(location = 2) in vec2 corner;  // side (-1/+1), end (0 at A, 1 at B)
layout(location = 3) in vec4 color;
layout(location = 4) in float width;  // pixels

layout(location = 0) out vec4 vColor;

layout(std140, binding = 0) uniform Uniforms {
    mat4 modelViewProjection;
    vec4 viewport;  // width, height in pixels
} ubuf;

void main()
{
    vec4 clipA = ubuf.modelViewProjection * vec4(pointA, 1.0);
    vec4 clipB = ubuf.modelViewProjection * vec4(pointB, 1.0);

    // An endpoint behind the camera is moved along the segment to just in front of it
    const float nearW = 1e-3;
    if (clipA.w < nearW) {
        clipA = mix(clipA, clipB, (nearW - clipA.w) / (clipB.w - clipA.w));
    } else if (clipB.w < nearW) {
        clipB = mix(clipB, clipA, (nearW - clipB.w) / (clipA.w - clipB.w));
    }

    vec2 screenA = clipA.xy / clipA.w * ubuf.viewport.xy;
    vec2 screenB = clipB.xy / clipB.w * ubuf.viewport.xy;
    vec2 direction = screenB - screenA;
    direction = length(direction) > 1e-6 ? normalize(direction) : vec2(1.0, 0.0);
    vec2 normal = vec2(-direction.y, direction.x);

    vec4 position = corner.y < 0.5 ? clipA : clipB;
    position.xy += normal * corner.x * width / ubuf.viewport.xy * position.w;
    vColor = color;
    gl_Position = position;
}
