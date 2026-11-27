#version 450
// PSGopher - the final blit's "Bicubic" and "Lanczos" filters: the picture resampled to the screen with a
// separable kernel, compiled twice (ps5/tools/make-shaders.sh): without LANCZOS, Catmull-Rom (the cubic
// with B = 0, C = 1/2 of Mitchell and Netravali, "Reconstruction Filters in Computer Graphics", 1988:
// sharp, and exact at the texels' centres); with LANCZOS, Lanczos with 3 lobes (sinc(x) sinc(x / 3)),
// its result held to the smallest and largest of the texels under the kernel's central lobe (the 2x2
// nearest texels when the picture is enlarged, see CENTRAL), which takes away the ringing (the light and dark halos)
// that its negative lobes put beside a hard edge.
//
// Enlarging and shrinking: the scale is the source texels per output pixel, taken from how fast the
// coordinate changes across a pixel (as Sharp Bilinear does, so it needs no push constant). When the
// picture is shrunk (more than one texel per pixel: 8x without Downsampling on a 2160-line screen), the
// kernel is widened by that scale, so that it filters the texels that fall in a pixel instead of
// skipping between them (aliasing). The widening is capped at MAX_TAPS taps per axis (Lanczos to 4 texels
// a pixel, Catmull-Rom to 6), past which it filters a little less than it should.
//
// Texels are read with texelFetch, their coordinates clamped to the picture (as a clamp-to-edge sampler
// does); the sampler bound is not used. The weights are normalised, so a flat area stays exactly flat.
// The vertex shader is the plain blit's (data/shader/main.vert).
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional term (GPL-3.0 section 7(b)): keep the attribution "PSGopher by Press5elect"; see NOTICE.

layout(location = 0) out vec4 FragColor;
layout(set = 0, binding = 0) uniform sampler2D uTexture;
layout(location = 0) in vec2 vUV;

const int MAX_TAPS = 24; // per axis

#ifdef LANCZOS
const float RADIUS = 3.0;
// The clamp's texels are those closer than CENTRAL * s: just under 1, so that at a position on a texel's
// centre (t whole, give or take a float's rounding) the texels one away, whose weight is 0 there, do not
// widen the clamp to a third row or column and let the horizontal (or vertical) ringing through.
const float CENTRAL = 0.999;
float weight(float x)
{
	x = abs(x);
	if (x >= RADIUS)
		return 0.0;
	if (x < 1e-5)
		return 1.0;
	const float px = 3.14159265358979 * x;
	return RADIUS * sin(px) * sin(px / RADIUS) / (px * px);
}
#else
const float RADIUS = 2.0;
float weight(float x)
{
	x = abs(x);
	if (x < 1.0)
		return (1.5 * x - 2.5) * x * x + 1.0;
	if (x < 2.0)
		return ((-0.5 * x + 2.5) * x - 4.0) * x + 2.0;
	return 0.0;
}
#endif

void main()
{
	const ivec2 isize = textureSize(uTexture, 0);
	const vec2 size = vec2(isize);
	const vec2 t = vUV * size - 0.5; // the position in texel-centre coordinates (texel i's centre at i)
	const vec2 step = abs(vec2(dFdx(vUV.x), dFdy(vUV.y))) * size;
	const vec2 s = clamp(step, vec2(1.0), vec2(float(MAX_TAPS) / (2.0 * RADIUS))); // the kernel's widening
	const vec2 support = RADIUS * s;
	// the texels i with |i - t| < support (an open interval of 2 * support <= MAX_TAPS texels)
	const ivec2 first = ivec2(floor(t - support)) + 1;
	const ivec2 count = min(ivec2(ceil(t + support)) - first, ivec2(MAX_TAPS));

	float wx[MAX_TAPS], wy[MAX_TAPS];
	float sx = 0.0, sy = 0.0;
	for (int i = 0; i < count.x; i++) {
		wx[i] = weight((float(first.x + i) - t.x) / s.x);
		sx += wx[i];
	}
	for (int j = 0; j < count.y; j++) {
		wy[j] = weight((float(first.y + j) - t.y) / s.y);
		sy += wy[j];
	}

	vec4 sum = vec4(0.0);
#ifdef LANCZOS
	vec4 lo = vec4(1e9), hi = vec4(-1e9);
#endif
	for (int j = 0; j < count.y; j++) {
		const int y = clamp(first.y + j, 0, isize.y - 1);
		vec4 row = vec4(0.0);
		for (int i = 0; i < count.x; i++) {
			const vec4 c = texelFetch(uTexture, ivec2(clamp(first.x + i, 0, isize.x - 1), y), 0);
			row += wx[i] * c;
#ifdef LANCZOS
			// the central lobe's texels: |i - t| < CENTRAL * s on both axes (the 2x2 nearest when enlarging)
			if (abs(float(first.x + i) - t.x) < CENTRAL * s.x && abs(float(first.y + j) - t.y) < CENTRAL * s.y) {
				lo = min(lo, c);
				hi = max(hi, c);
			}
#endif
		}
		sum += wy[j] * row;
	}
	vec4 c = sum / (sx * sy);
#ifdef LANCZOS
	c = clamp(c, lo, hi);
#endif
	FragColor = clamp(c, 0.0, 1.0);
}
