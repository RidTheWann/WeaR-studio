#version 440

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform FilterBlock {
    vec4 filter0;
    vec4 filter1;
    vec4 filter2;
    vec4 filter3;
};

layout(binding = 1) uniform sampler2D sourceTexture;

float luminance(vec3 color)
{
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

void main()
{
    vec4 color = texture(sourceTexture, v_uv);
    int kind = int(filter0.x + 0.5);

    if (kind == 1) {
        // Chroma key: filter1.rgb = key color, filter0.y/z = threshold/softness.
        float distanceToKey = distance(color.rgb, filter1.rgb);
        float threshold = filter0.y;
        float softness = max(filter0.z, 0.0001);
        float keep = smoothstep(threshold - softness,
                                 threshold + softness,
                                 distanceToKey);
        color.a *= keep;
    } else if (kind == 2) {
        // 3x3 Gaussian kernel. filter0.y is the pixel radius.
        float radius = max(filter0.y, 1.0);
        vec2 texel = vec2(filter3.y, filter3.z) * radius;

        vec4 sum = vec4(0.0);
        sum += texture(sourceTexture, v_uv + vec2(-texel.x, -texel.y)) * 1.0;
        sum += texture(sourceTexture, v_uv + vec2( 0.0,      -texel.y)) * 2.0;
        sum += texture(sourceTexture, v_uv + vec2( texel.x, -texel.y)) * 1.0;
        sum += texture(sourceTexture, v_uv + vec2(-texel.x,  0.0)) * 2.0;
        sum += color * 4.0;
        sum += texture(sourceTexture, v_uv + vec2( texel.x,  0.0)) * 2.0;
        sum += texture(sourceTexture, v_uv + vec2(-texel.x,  texel.y)) * 1.0;
        sum += texture(sourceTexture, v_uv + vec2( 0.0,       texel.y)) * 2.0;
        sum += texture(sourceTexture, v_uv + vec2( texel.x,  texel.y)) * 1.0;
        color = sum / 16.0;
    } else if (kind == 3) {
        // Color correction: filter0.y = brightness, filter0.z = contrast,
        // filter0.w = saturation, filter1.x = gamma.
        float brightness = filter0.y;
        float contrast = filter0.z;
        float saturation = filter0.w;
        float gamma = max(filter1.x, 0.1);

        vec3 corrected = color.rgb + vec3(brightness);
        corrected = (corrected - vec3(0.5)) * contrast + vec3(0.5);

        float luma = luminance(corrected);
        corrected = vec3(luma) + (corrected - vec3(luma)) * saturation;
        corrected = pow(clamp(corrected, 0.0, 1.0), vec3(1.0 / gamma));
        color.rgb = corrected;
    }

    color.a *= clamp(filter3.x, 0.0, 1.0);
    fragColor = color;
}
