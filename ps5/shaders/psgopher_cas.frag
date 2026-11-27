#version 450
// PSGopher - Sharpening: AMD FidelityFX Contrast Adaptive Sharpening (CAS), its sharpen-only path
// (no scaling), at the screen's resolution after the picture has been scaled to it (and after Post
// Anti-Aliasing), before the on-screen messages are drawn over it.
//
// The algorithm is AMD's, from FidelityFX CAS 1.20190610 (ffx-cas/ffx_cas.h, CasSetup and CasFilter with
// noScaling true; https://github.com/GPUOpen-Effects/FidelityFX-CAS), ported to plain GLSL:
//   - the default 5-tap "circle" (CAS_BETTER_DIAGONALS not defined) and the green channel's weight for
//     all three channels (CAS_SLOW not defined);
//   - exact reciprocals and square roots (CAS_GO_SLOWER's path) in place of ffx_a.h's approximations;
//     the reciprocal of the soft maximum is taken of max(mx, 1e-20), so a black neighbourhood gives 0
//     and not 0 * infinity (its result is the same: all five texels are 0);
//   - the input linearised with gamma 2.0 (CasInput squares it) and the output encoded back with a
//     square root: AMD's own integration for 8-bit UNORM input ("UNORM with linear conversion
//     approximation"), since CAS is "designed to be a linear filter". With HDR10 this pass runs on the
//     SDR picture before the HDR pass turns it into PQ (AMD: CAS "does not function on PQ non-linear data").
// The sharpness knob is CasSetup's: peak = -1 / mix(8, 5, sharpness), sharpness 0 (AMD's default,
// lower ringing) to 1 (its maximum); the title passes the peak (push constant params.x).
//
// Texels outside the picture's rectangle on the screen (u.rect, in pixels: the letterbox is not
// sharpened against) are read as the rectangle's edge, as a clamp-to-edge sampler does.
//
// AMD's notice for the algorithm:
// Copyright (c) 2017-2019 Advanced Micro Devices, Inc. All rights reserved.
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation
// files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
// Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
// WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
// PSGopher's port: SPDX-License-Identifier: GPL-3.0-or-later AND MIT (AMD's algorithm, MIT; see NOTICE)
// Additional term (GPL-3.0 section 7(b)): keep the attribution "PSGopher by Press5elect"; see NOTICE.

layout(push_constant) uniform Push
{
	vec4 rect;   // the picture on the screen: x0, y0, x1, y1 in pixels (x1, y1 past its last pixel)
	vec4 params; // x: CAS's peak, -1 / mix(8, 5, sharpness)
} u;

layout(set = 0, binding = 0) uniform sampler2D uTexture; // the scaled picture, the screen's size
layout(location = 0) out vec4 FragColor;

ivec2 lo_px, hi_px;

vec3 casLoad(ivec2 p)
{
	const vec3 c = texelFetch(uTexture, clamp(p, lo_px, hi_px), 0).rgb;
	return c * c; // CasInput: gamma 2.0 to linear
}

void main()
{
	lo_px = ivec2(u.rect.xy);
	hi_px = ivec2(u.rect.zw) - 1;
	const ivec2 sp = ivec2(gl_FragCoord.xy);
	//    b
	//  d e f
	//    h
	const vec3 b = casLoad(sp + ivec2(0, -1));
	const vec3 d = casLoad(sp + ivec2(-1, 0));
	const vec3 e = casLoad(sp);
	const vec3 f = casLoad(sp + ivec2(1, 0));
	const vec3 h = casLoad(sp + ivec2(0, 1));
	// soft minimum and maximum (AMin3F1(AMin3F1(d, e, f), b, h))
	const vec3 mn = min(min(min(d, e), f), min(b, h));
	const vec3 mx = max(max(max(d, e), f), max(b, h));
	// the smallest distance to the signal's limits over the maximum, shaped with a square root
	const vec3 amp = sqrt(clamp(min(mn, 1.0 - mx) / max(mx, vec3(1e-20)), 0.0, 1.0));
	// the filter  0 w 0 / w 1 w / 0 w 0, with the green channel's w for all three
	const float w = amp.g * u.params.x;
	const float rcpWeight = 1.0 / (1.0 + 4.0 * w);
	const vec3 pix = clamp(((b + d + f + h) * w + e) * rcpWeight, 0.0, 1.0);
	FragColor = vec4(sqrt(pix), 1.0); // linear to gamma 2.0
}
