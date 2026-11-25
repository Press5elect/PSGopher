#version 450
// PSGopher - HDR10 output: the frame's two layers (the game's picture, and the on-screen messages over
// clear 0) made into HDR10's pixels for framebuffers the graphics driver has retyped as HDR10 (Crux2's
// RADV, ps5/patches/mesa-27/0020 in Crux2: 10:10:10:2, B in the low bits, BT.2020, PQ).
//
// The light is Crux2's (ps5/package/resources/shaders/vulkan/ps5_final.glsl, FINAL_PASS 1; Crux2 by
// Press5elect, GPL-3.0-or-later, after OGBox): SDR values decoded with a 2.2 gamma, BT.709 to BT.2020,
// white at metrics.x nits (203 by default, ITU-R BT.2408), the picture's highlights raised above a
// knee toward metrics.y nits, the messages exactly at white, then SMPTE ST 2084's PQ curve.
//
// The difference from Crux2: no 10-bit image and copy. The swapchain stays B8G8R8A8_UNORM, and this
// pass writes the four bytes of each 10:10:10:2 word into it directly (each written as byte/255, which
// the unorm conversion stores exactly): the same bits reach the framebuffer.
//
// SPDX-License-Identifier: GPL-3.0-or-later

layout(push_constant) uniform Push
{
	vec4 metrics; // x white nits, y highlights' peak nits (at or below white: no highlights)
} u;

layout(set = 0, binding = 0) uniform sampler2D picture; // the game's picture, as for an SDR screen
layout(set = 0, binding = 1) uniform sampler2D overlay; // the messages, over clear 0 (alpha: their cover)

layout(location = 0) out vec4 o_col0;

const float M1 = 2610.0 / 16384.0;
const float M2 = 2523.0 / 4096.0 * 128.0;
const float C1 = 3424.0 / 4096.0;
const float C2 = 2413.0 / 4096.0 * 32.0;
const float C3 = 2392.0 / 4096.0 * 32.0;
const float KNEE = 0.5;

// BT.709 to BT.2020 (ITU-R BT.2087), columns as GLSL takes them
const mat3 BT709_TO_2020 = mat3(
	0.6274, 0.0691, 0.0164,
	0.3293, 0.9195, 0.0880,
	0.0433, 0.0114, 0.8956);

vec3 pq(vec3 nits)
{
	const vec3 y = pow(clamp(nits / 10000.0, 0.0, 1.0), vec3(M1));
	return pow((C1 + C2 * y) / (1.0 + C3 * y), vec3(M2));
}

vec3 decode(vec3 c)
{
	return pow(max(c, vec3(0.0)), vec3(2.2));
}

void main()
{
	const ivec2 at = ivec2(gl_FragCoord.xy);
	const vec3 p = texelFetch(picture, at, 0).rgb;
	const vec4 m = texelFetch(overlay, at, 0);
	const float white = max(u.metrics.x, 1.0);
	float gain = 1.0;
	if (u.metrics.y > white)
	{
		const float l = dot(decode(p), vec3(0.2126, 0.7152, 0.0722));
		if (l > KNEE)
		{
			const float t = (min(l, 1.0) - KNEE) / (1.0 - KNEE);
			gain = (l + (u.metrics.y / white - 1.0) * t * t) / l;
		}
	}
	const vec3 sdr = m.rgb + p * (1.0 - m.a);
	const vec3 light = decode(sdr) * mix(gain, 1.0, m.a);
	const vec3 e = pq(max(BT709_TO_2020 * light, vec3(0.0)) * white);

	// 10:10:10:2 with B in the low bits, alpha 3; its bytes into the B8G8R8A8 attachment's channels
	const uvec3 q = uvec3(round(clamp(e, 0.0, 1.0) * 1023.0));
	const uint word = (3u << 30) | (q.r << 20) | (q.g << 10) | q.b;
	o_col0 = vec4(float((word >> 16) & 255u), float((word >> 8) & 255u), float(word & 255u), float(word >> 24)) / 255.0;
}
