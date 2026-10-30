#version 440

// Cloud from a tiling density texture sampled three times: the shape, finer detail (each drifting with the wind)
// and a much larger weather pattern that moves the coverage. Cut off below the coverage so sky shows between
// clouds, darker where thicker as seen from below, thinned out with distance and fogged into the horizon colour.

layout(location = 0) in vec3 vPosition;

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform Uniforms {
    mat4 modelViewProjection;
    vec4 camera;
    vec4 scaleOffset;
    vec4 detail;
    vec4 weather;
    vec4 shape;
    vec4 fade;
    vec4 fogColor;
    vec4 litColor;
    vec4 shadeColor;
} ubuf;

layout(binding = 1) uniform sampler2D density;

// Fragments fainter than this are dropped rather than blended
const float minAlpha = 0.004;

void main()
{
    vec2 uv = vPosition.xy * ubuf.scaleOffset.xy;
    float shapeDensity = texture(density, uv + ubuf.scaleOffset.zw).r;
    float detailDensity = texture(density, uv * ubuf.detail.x + ubuf.detail.yz).r;
    float cloudDensity = mix(shapeDensity, detailDensity, ubuf.detail.w);
    float weatherValue = texture(density, uv * ubuf.weather.x + ubuf.weather.yz).r;
    float coverage = ubuf.shape.x + (0.5 - weatherValue) * 2.0 * ubuf.weather.w;
    float cloud = smoothstep(coverage, coverage + ubuf.shape.y, cloudDensity);
    float across = length(vPosition.xy - ubuf.camera.xy);
    float alpha = cloud * ubuf.shape.z * (1.0 - smoothstep(ubuf.fade.x, ubuf.fade.y, across));
    if (alpha < minAlpha) {
        discard;
    }
    vec3 color = mix(ubuf.litColor.rgb, ubuf.shadeColor.rgb, smoothstep(coverage, 1.0, cloudDensity));
    float fog = smoothstep(ubuf.fade.z, ubuf.fade.w, length(vPosition - ubuf.camera.xyz));
    fragColor = vec4(mix(color, ubuf.fogColor.rgb, fog), alpha);
}
