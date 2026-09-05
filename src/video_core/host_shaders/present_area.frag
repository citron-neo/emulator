#version 460 core

layout(location = 0) in vec2 frag_tex_coord;
layout(location = 0) out vec4 color;
layout(binding = 0) uniform sampler2D color_texture;

#ifdef VULKAN
struct ScreenRectVertex {
    vec2 position;
    vec2 tex_coord;
};
layout (push_constant) uniform PushConstants {
    mat4 modelview_matrix;
    ScreenRectVertex vertices[4];
};
#else // OpenGL
layout(location = 1) uniform uvec2 screen_size;
#endif

/***** Area Sampling *****/
// By Sam Belliveau and Filippo Tarpini. Public Domain license.
// Effectively a more accurate sharp bilinear filter when upscaling,
// that also works as a mathematically perfect downscale filter.
// https://entropymine.com/imageworsener/pixelmixing/
// https://github.com/obsproject/obs-studio/pull/1715
// https://legacy.imagemagick.org/Usage/filter/
vec4 AreaSampling(sampler2D s, vec2 tc, vec4 trans_bounds) {
    // Compute the top-left and bottom-right corners of the pixel box.
    ivec4 b = ivec4(floor(trans_bounds));
    // Compute how much of the start and end pixels are covered horizontally & vertically.
    // W,N,E,S
    vec4 kb = vec4(1.0f - fract(trans_bounds.xy), fract(trans_bounds.zw));
    // Compute the areas of the corner pixels in the pixel box.
    // NW,NE,SW,SE
    vec4 kc = kb.yyww * kb.xzxz;
    // Accumulate corner pixels by forming a corner matrix.
    vec4 r = mat4x4(
        texelFetch(s, ivec2(b.xy), 0),
        texelFetch(s, ivec2(b.zy), 0),
        texelFetch(s, ivec2(b.xw), 0),
        texelFetch(s, ivec2(b.zw), 0)
    ) * kc;
    // Determine the size of the pixel box.
    ivec2 q = clamp(ivec2(
        int(b.z - b.x - 0.5f),
        int(b.w - b.y - 0.5f)
    ), ivec2(-16), ivec2(16));
    vec2 qf = vec2(q);
    // Accumulate top and bottom edge pixels.
    for (int x = 0; x < q.x; ++x) {
        r += kb.y * texelFetch(s, ivec2(x + b.x + 1, b.y), 0);
        r += kb.w * texelFetch(s, ivec2(x + b.x + 1, b.w), 0);
    }
    // Accumulate left and right edge pixels and all the pixels in between.
    for (int y = 0; y < q.y; ++y) {
        r += kb.x * texelFetch(s, ivec2(b.x, y + b.y + 1), 0);
        r += kb.z * texelFetch(s, ivec2(b.z, y + b.y + 1), 0);
        for (int x = 0; x < q.x; ++x) {
            r += texelFetch(s, ivec2(x, y) + b.xy + 1, 0);
        }
    }
    // Compute the area of the pixel box that was sampled.
    // Return the normalized average color.
    return r / (
        kc.x + kc.y + kc.z + kc.w //corners
        + qf.x * (kb.y + kb.w) + qf.y * (kb.x + kb.z) //edges
        + qf.x * qf.y // center
    );
}

void main() {
#ifdef VULKAN
    vec2 dst_size = vec2(
        vertices[1].position.x - vertices[0].position.x,
        vertices[2].position.y - vertices[0].position.y
    );
#else // OpenGL
    vec2 dst_size = screen_size;
#endif

    vec2 src_size = textureSize(color_texture, 0);
    // Determine the range of the source image that the target pixel will cover.
    vec2 scale = src_size * (1.0f / dst_size);
    color = AreaSampling(color_texture, frag_tex_coord, vec4(
        (frag_tex_coord * src_size) - (scale * 0.5f),
        // (scale * 0.5f) + scale
        // {A / 2 + A} ==> {(A + 2A) / 2} ==> {3A / 2} ==> {A * (3/2)}
        (frag_tex_coord * src_size) + scale * 0.5f
    ));
}
