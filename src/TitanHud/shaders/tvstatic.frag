#version 440

// Old-television static: grey snow redrawn every frame, faint scanlines, a slow rolling band and a dim vignette

layout(location = 0) in vec2 qt_TexCoord0;

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float seed;       // a new random value each frame
    float time;       // seconds, for the rolling band
    vec2 resolution;  // item size in logical pixels
};

// Hash without sine, so every GPU draws the same snow
float hash(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

void main()
{
    const float grain = 2.0;
    vec2 block = floor(qt_TexCoord0 * resolution / grain);
    float snow = hash(block + vec2(seed * 13.7, seed * 7.3));
    float value = mix(0.08, 0.85, snow);

    float line = floor(qt_TexCoord0.y * resolution.y);
    value *= (mod(line, 2.0) < 1.0) ? 1.0 : 0.88;

    float band = fract(qt_TexCoord0.y - time * 0.12) - 0.5;
    value += 0.1 * exp(-band * band * 60.0);

    vec2 centered = qt_TexCoord0 - 0.5;
    value *= 1.0 - 0.45 * dot(centered, centered);

    fragColor = vec4(vec3(value), 1.0) * qt_Opacity;
}
