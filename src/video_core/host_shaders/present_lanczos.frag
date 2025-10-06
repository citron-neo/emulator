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

// --- Helper Functions for Advanced Quality ---
vec3 to_linear(vec3 srgb_color) { return pow(srgb_color, vec3(2.2)); }
vec3 to_srgb(vec3 linear_color) { return pow(linear_color, vec3(1.0 / 2.2)); }

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

	vec4 sum = vec4(0.0);
	float weight_sum = 0.0;

	vec3 min_color_linear = vec3(1.0);
	vec3 max_color_linear = vec3(0.0);

	// Get the center texel's color to help with the adaptive clamp.
	vec3 center_color_linear = to_linear(texture(ts, (p_int + 0.5) * inv_tex_size).rgb);

	for (int y = -a_val + 1; y <= a_val; ++y) {
		for (int x = -a_val + 1; x <= a_val; ++x) {
			vec2 offset = vec2(float(x), float(y));

			float w = lanczos_weight(f.x - offset.x, a) * lanczos_weight(f.y - offset.y, a);

			if (w != 0.0) {
				vec2 sample_coord = (p_int + offset + 0.5) * inv_tex_size;
				vec4 srgb_sample = texture(ts, sample_coord);
				vec3 linear_sample_rgb = to_linear(srgb_sample.rgb);

				min_color_linear = min(min_color_linear, linear_sample_rgb);
				max_color_linear = max(max_color_linear, linear_sample_rgb);

				sum.rgb += linear_sample_rgb * w;
				sum.a += srgb_sample.a * w;
				weight_sum += w;
			}
		}
	}


	if (weight_sum == 0.0) return texture(ts, tc);

	vec4 final_color_linear = sum / weight_sum;

	// This is a more effective "Adaptive Anti-Ringing Clamp".
	vec3 adaptive_min_bound = (min_color_linear + center_color_linear) * 0.5;
	vec3 adaptive_max_bound = (max_color_linear + center_color_linear) * 0.5;
	final_color_linear.rgb = clamp(final_color_linear.rgb, adaptive_min_bound, adaptive_max_bound);

	final_color_linear.rgb = to_srgb(final_color_linear.rgb);

	return final_color_linear;
}

void main() {
	color = textureLanczos(color_texture, frag_tex_coord);
}
