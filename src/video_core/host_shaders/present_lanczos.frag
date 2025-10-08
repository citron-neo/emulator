// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#version 460 core

layout(location = 0) in vec2 frag_tex_coord;
layout(location = 0) out vec4 color;

layout(binding = 0) uniform sampler2D color_texture;

#ifdef VULKAN
layout(push_constant) uniform LanczosPushConstant {
	layout(offset = 128) int u_lanczos_a; // The 'a' parameter, recommend 2 or 3.
} lanczos_pc;
#else // OpenGL
layout(location = 0) uniform int u_lanczos_a; // The 'a' parameter, recommend 2 or 3.
#endif

const float PI = 3.141592653589793;

// --- sRGB/Linear Conversions ---
// Replaces the unstable pow() functions with the standard, piecewise formulas.

// Converts a color from sRGB space to linear space.
vec3 to_linear(vec3 srgb) {
    bvec3 cutoff = lessThan(srgb, vec3(0.04045));
    vec3 higher = pow((srgb + vec3(0.055)) / vec3(1.055), vec3(2.4));
    vec3 lower = srgb / vec3(12.92);
    return mix(higher, lower, cutoff);
}

// Converts a color from linear space to sRGB space.
vec3 to_srgb(vec3 linear) {
    // Safety clamp to ensure the input is valid before conversion.
    linear = clamp(linear, 0.0, 1.0);
    bvec3 cutoff = lessThan(linear, vec3(0.0031308));
    vec3 higher = vec3(1.055) * pow(linear, vec3(1.0 / 2.4)) - vec3(0.055);
    vec3 lower = linear * vec3(12.92);
    return mix(higher, lower, cutoff);
}

// Sinc function
float sinc(float x) {
	if (x == 0.0) return 1.0;
	float pix = PI * x;
	return sin(pix) / pix;
}

// Lanczos windowed sinc function
float lanczos_weight(float x, float a) {
	if (abs(x) < a) return sinc(x) * sinc(x / a);
	return 0.0;
}

vec4 textureLanczos(sampler2D ts, vec2 tc) {
	#ifdef VULKAN
	const int a_val = lanczos_pc.u_lanczos_a;
	#else
	const int a_val = u_lanczos_a;
	#endif

	if (a_val < 1) return texture(ts, tc);

	const float a = float(a_val);
	vec2 tex_size = vec2(textureSize(ts, 0));
	vec2 inv_tex_size = 1.0 / tex_size;

	vec2 p = tc * tex_size - 0.5;
	vec2 f = fract(p);
	vec2 p_int = p - f;

	vec3 sum_rgb = vec3(0.0);
	float sum_a = 0.0;
	float weight_sum = 0.0;

	for (int y = -a_val + 1; y <= a_val; ++y) {
		for (int x = -a_val + 1; x <= a_val; ++x) {
			vec2 offset = vec2(float(x), float(y));
			float w = lanczos_weight(f.x - offset.x, a) * lanczos_weight(f.y - offset.y, a);

			if (w != 0.0) {
				vec2 sample_coord = (p_int + offset + 0.5) * inv_tex_size;
				vec4 srgb_sample = texture(ts, sample_coord);

				sum_rgb += to_linear(srgb_sample.rgb) * w;
				sum_a += srgb_sample.a * w;
				weight_sum += w;
			}
		}
	}

	if (abs(weight_sum) < 0.0001) {
		return texture(ts, tc);
	}

	// Normalize the sums.
	vec3 final_rgb_linear = sum_rgb / weight_sum;
	float final_a = sum_a / weight_sum;

	// Convert back to sRGB. The to_srgb function now contains its own
    // safety clamp, making this step robust.
	vec3 final_rgb_srgb = to_srgb(final_rgb_linear);

	return vec4(final_rgb_srgb, clamp(final_a, 0.0, 1.0));
}

void main() {
	color = textureLanczos(color_texture, frag_tex_coord);
}
