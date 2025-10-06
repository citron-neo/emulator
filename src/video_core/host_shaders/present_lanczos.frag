// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#version 460 core

layout(location = 0) in vec2 frag_tex_coord;
layout(location = 0) out vec4 color;

layout(binding = 0) uniform sampler2D color_texture;

// This block defines the variable that will hold our setting value.
// It uses a preprocessor directive to switch between the OpenGL and Vulkan way of doing things.
#ifdef VULKAN
layout(push_constant) uniform LanczosPushConstant {
	layout(offset = 128) int u_lanczos_a;
} lanczos_pc;
#else // OpenGL
layout(location = 0) uniform int u_lanczos_a;
#endif

const float PI = 3.14159265359;

float sinc(float x) {
	if (x == 0.0) {
		return 1.0;
	}
	return sin(PI * x) / (PI * x);
}

float lanczos_weight(float x, float a) {
	if (abs(x) < a) {
		return sinc(x) * sinc(x / a);
	}
	return 0.0;
}

vec4 textureLanczos(sampler2D ts, vec2 tc) {
	// Get the 'a' value from the correct uniform based on the renderer
	#ifdef VULKAN
	const int a_val = lanczos_pc.u_lanczos_a;
	#else
	const int a_val = u_lanczos_a;
	#endif
	const float a = float(a_val);

	// If 'a' is 0 (which it will be by default since we aren't sending a value yet),
	// just do a basic sample to avoid errors.
	if (a_val == 0) {
		return texture(ts, tc);
	}

	vec2 tex_size = vec2(textureSize(ts, 0));
	vec2 inv_tex_size = 1.0 / tex_size;
	vec2 p = tc * tex_size;
	vec2 f = fract(p);
	vec2 p_int = p - f;

	vec4 sum = vec4(0.0);
	float weight_sum = 0.0;

	const int MAX_A = 4;
	for (int y = -MAX_A + 1; y <= MAX_A; ++y) {
		if (abs(y) >= a_val) continue;
		for (int x = -MAX_A + 1; x <= MAX_A; ++x) {
			if (abs(x) >= a_val) continue;

			vec2 offset = vec2(float(x), float(y));
			float w = lanczos_weight(f.x - offset.x, a) * lanczos_weight(f.y - offset.y, a);

			if (w != 0.0) {
				sum += texture(ts, (p_int + offset) * inv_tex_size) * w;
				weight_sum += w;
			}
		}
	}

	if (weight_sum == 0.0) {
		return texture(ts, tc);
	}

	return sum / weight_sum;
}

void main() {
	color = textureLanczos(color_texture, frag_tex_coord);
}
