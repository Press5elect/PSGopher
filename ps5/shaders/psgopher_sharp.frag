#version 450
// PSGopher - the final blit's "Sharp Bilinear" filter: the picture scaled to the screen with its texels
// kept crisp. Each texel's centre is shown flat, and the colours blend only across a band one output
// pixel wide at each texel's edge, so the picture is neither blurred (as bilinear does) nor uneven
// (as nearest does at a scale that is not a whole number). At a whole-number scale it equals nearest.
// Drawn with a linear sampler; the vertex shader is the plain blit's (data/shader/main.vert).
//
// The scale is the output pixels per texel, taken from how fast the coordinate changes across a pixel,
// so it needs no push constant. Below 1 (the picture shrunk) it is 1, which is plain bilinear.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional term (GPL-3.0 section 7(b)): keep the attribution "PSGopher by Press5elect"; see NOTICE.

layout(location = 0) out vec4 FragColor;
layout(set = 0, binding = 0) uniform sampler2D uTexture;
layout(location = 0) in vec2 vUV;

void main()
{
	const vec2 size = vec2(textureSize(uTexture, 0));
	const vec2 texel = vUV * size;
	const vec2 step = max(abs(vec2(dFdx(vUV.x), dFdy(vUV.y))), vec2(1e-6));
	const vec2 scale = max(1.0 / (size * step), vec2(1.0));
	const vec2 range = 0.5 - 0.5 / scale;
	const vec2 offset = fract(texel) - 0.5;
	const vec2 inside = (offset - clamp(offset, -range, range)) * scale + 0.5;
	FragColor = textureLod(uTexture, (floor(texel) + inside) / size, 0.0);
}
