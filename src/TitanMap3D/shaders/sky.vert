#version 440

// Full-screen sky: each corner carries its world view direction, so the gradient follows the real horizon

layout(location = 0) in vec2 position;
layout(location = 1) in vec3 direction;

layout(location = 0) out vec3 vDirection;

void main()
{
    vDirection = direction;
    gl_Position = vec4(position, 0.0, 1.0);
}
