#version 450
// PSGopher - Post Anti-Aliasing: NVIDIA FXAA 3.11 by Timothy Lottes, its PC "Quality" algorithm
// (FxaaPixelShader with FXAA_PC 1) at quality preset 29 (12 steps along an edge), at the screen's
// resolution after the picture has been scaled to it, before Sharpening and the on-screen messages.
//
// Ported from FXAA3_11.h as NVIDIA published it in its GameWorks Graphics Samples
// (samples/es3-kepler/FXAA/FXAA3_11.h, SDK v3.00, BSD 3-clause; the notice is below), with:
//   - FXAA_GATHER4_ALPHA 0, FXAA_DISCARD 0, FXAA_GREEN_AS_LUMA 0;
//   - the luma computed from each sample's colour, dot(rgb, (0.299, 0.587, 0.114)), in place of being
//     read from alpha: the picture is gamma-encoded, and this is the perceptual luma FXAA's integration
//     notes have the pass before it store ("color.a = dot(color.rgb, ...)"); luma is linear in the
//     colour, so a bilinear sample's luma is the luma the alpha channel would have given;
//   - the nested search written as a loop over the preset's steps (the same steps, in the same order);
//   - NVIDIA's default tuning: fxaaQualitySubpix 0.75, fxaaQualityEdgeThreshold 0.166,
//     fxaaQualityEdgeThresholdMin 0.0833;
//   - every sample's position clamped to the picture's rectangle on the screen (u.rect), so the
//     letterbox is not blended in.
// The sampler is bilinear (FXAA's integration checklist, 7).
//
// NVIDIA's notice for FXAA3_11.h:
// Copyright (c) 2014-2015, NVIDIA CORPORATION. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//  * Neither the name of NVIDIA CORPORATION nor the names of its
//    contributors may be used to endorse or promote products derived
//    from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
// OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// PSGopher's port: SPDX-License-Identifier: GPL-3.0-or-later AND BSD-3-Clause (NVIDIA's FXAA; see NOTICE)
// Additional term (GPL-3.0 section 7(b)): keep the attribution "PSGopher by Press5elect"; see NOTICE.

layout(push_constant) uniform Push
{
	vec4 rect;   // the picture on the screen: x0, y0, x1, y1 in pixels (x1, y1 past its last pixel)
	vec4 params; // (unused)
} u;

layout(set = 0, binding = 0) uniform sampler2D tex; // the scaled picture, the screen's size; linear sampler
layout(location = 0) out vec4 FragColor;

const float SUBPIX = 0.75;
const float EDGE_THRESHOLD = 0.166;
const float EDGE_THRESHOLD_MIN = 0.0833;
// FXAA_QUALITY__PRESET 29
const int PS = 12;
const float P[12] = float[](1.0, 1.5, 2.0, 2.0, 2.0, 2.0, 2.0, 2.0, 2.0, 2.0, 4.0, 8.0);

vec2 lo_uv, hi_uv;

vec4 top(vec2 p) { return textureLod(tex, clamp(p, lo_uv, hi_uv), 0.0); }
float luma(vec4 rgba) { return dot(rgba.rgb, vec3(0.299, 0.587, 0.114)); }
float lumaAt(vec2 p) { return luma(top(p)); }

void main()
{
	const vec2 rcpFrame = 1.0 / vec2(textureSize(tex, 0));
	lo_uv = (u.rect.xy + 0.5) * rcpFrame;
	hi_uv = (u.rect.zw - 0.5) * rcpFrame;
	vec2 posM = gl_FragCoord.xy * rcpFrame;

	const vec4 rgbyM = top(posM);
	const float lumaM = luma(rgbyM);
	float lumaS = lumaAt(posM + vec2(0.0, 1.0) * rcpFrame);
	float lumaE = lumaAt(posM + vec2(1.0, 0.0) * rcpFrame);
	float lumaN = lumaAt(posM + vec2(0.0, -1.0) * rcpFrame);
	float lumaW = lumaAt(posM + vec2(-1.0, 0.0) * rcpFrame);

	const float maxSM = max(lumaS, lumaM);
	const float minSM = min(lumaS, lumaM);
	const float maxESM = max(lumaE, maxSM);
	const float minESM = min(lumaE, minSM);
	const float maxWN = max(lumaN, lumaW);
	const float minWN = min(lumaN, lumaW);
	const float rangeMax = max(maxWN, maxESM);
	const float rangeMin = min(minWN, minESM);
	const float rangeMaxScaled = rangeMax * EDGE_THRESHOLD;
	const float range = rangeMax - rangeMin;
	const float rangeMaxClamped = max(EDGE_THRESHOLD_MIN, rangeMaxScaled);
	if (range < rangeMaxClamped) {
		FragColor = vec4(rgbyM.rgb, 1.0); // (early exit: no edge here)
		return;
	}

	const float lumaNW = lumaAt(posM + vec2(-1.0, -1.0) * rcpFrame);
	const float lumaSE = lumaAt(posM + vec2(1.0, 1.0) * rcpFrame);
	const float lumaNE = lumaAt(posM + vec2(1.0, -1.0) * rcpFrame);
	const float lumaSW = lumaAt(posM + vec2(-1.0, 1.0) * rcpFrame);

	const float lumaNS = lumaN + lumaS;
	const float lumaWE = lumaW + lumaE;
	const float subpixRcpRange = 1.0 / range;
	const float subpixNSWE = lumaNS + lumaWE;
	const float edgeHorz1 = (-2.0 * lumaM) + lumaNS;
	const float edgeVert1 = (-2.0 * lumaM) + lumaWE;

	const float lumaNESE = lumaNE + lumaSE;
	const float lumaNWNE = lumaNW + lumaNE;
	const float edgeHorz2 = (-2.0 * lumaE) + lumaNESE;
	const float edgeVert2 = (-2.0 * lumaN) + lumaNWNE;

	const float lumaNWSW = lumaNW + lumaSW;
	const float lumaSWSE = lumaSW + lumaSE;
	const float edgeHorz4 = (abs(edgeHorz1) * 2.0) + abs(edgeHorz2);
	const float edgeVert4 = (abs(edgeVert1) * 2.0) + abs(edgeVert2);
	const float edgeHorz3 = (-2.0 * lumaW) + lumaNWSW;
	const float edgeVert3 = (-2.0 * lumaS) + lumaSWSE;
	const float edgeHorz = abs(edgeHorz3) + edgeHorz4;
	const float edgeVert = abs(edgeVert3) + edgeVert4;

	const float subpixNWSWNESE = lumaNWSW + lumaNESE;
	float lengthSign = rcpFrame.x;
	const bool horzSpan = edgeHorz >= edgeVert;
	const float subpixA = subpixNSWE * 2.0 + subpixNWSWNESE;

	if (!horzSpan) lumaN = lumaW;
	if (!horzSpan) lumaS = lumaE;
	if (horzSpan) lengthSign = rcpFrame.y;
	const float subpixB = (subpixA * (1.0 / 12.0)) - lumaM;

	const float gradientN = lumaN - lumaM;
	const float gradientS = lumaS - lumaM;
	float lumaNN = lumaN + lumaM;
	const float lumaSS = lumaS + lumaM;
	const bool pairN = abs(gradientN) >= abs(gradientS);
	const float gradient = max(abs(gradientN), abs(gradientS));
	if (pairN) lengthSign = -lengthSign;
	const float subpixC = clamp(abs(subpixB) * subpixRcpRange, 0.0, 1.0);

	vec2 posB = posM;
	vec2 offNP;
	offNP.x = (!horzSpan) ? 0.0 : rcpFrame.x;
	offNP.y = (horzSpan) ? 0.0 : rcpFrame.y;
	if (!horzSpan) posB.x += lengthSign * 0.5;
	if (horzSpan) posB.y += lengthSign * 0.5;

	vec2 posN = posB - offNP * P[0];
	vec2 posP = posB + offNP * P[0];
	const float subpixD = ((-2.0) * subpixC) + 3.0;
	float lumaEndN = lumaAt(posN);
	const float subpixE = subpixC * subpixC;
	float lumaEndP = lumaAt(posP);

	if (!pairN) lumaNN = lumaSS;
	const float gradientScaled = gradient * 1.0 / 4.0;
	const float lumaMM = lumaM - lumaNN * 0.5;
	const float subpixF = subpixD * subpixE;
	const bool lumaMLTZero = lumaMM < 0.0;

	lumaEndN -= lumaNN * 0.5;
	lumaEndP -= lumaNN * 0.5;
	bool doneN = abs(lumaEndN) >= gradientScaled;
	bool doneP = abs(lumaEndP) >= gradientScaled;
	if (!doneN) posN -= offNP * P[1];
	bool doneNP = (!doneN) || (!doneP);
	if (!doneP) posP += offNP * P[1];

	// the preset's further steps (FXAA3_11.h's nested "if(doneNP)" blocks, P2 to P11)
	for (int k = 2; k < PS; k++) {
		if (!doneNP)
			break;
		if (!doneN) lumaEndN = lumaAt(posN);
		if (!doneP) lumaEndP = lumaAt(posP);
		if (!doneN) lumaEndN = lumaEndN - lumaNN * 0.5;
		if (!doneP) lumaEndP = lumaEndP - lumaNN * 0.5;
		doneN = abs(lumaEndN) >= gradientScaled;
		doneP = abs(lumaEndP) >= gradientScaled;
		if (!doneN) posN -= offNP * P[k];
		doneNP = (!doneN) || (!doneP);
		if (!doneP) posP += offNP * P[k];
	}

	float dstN = posM.x - posN.x;
	float dstP = posP.x - posM.x;
	if (!horzSpan) dstN = posM.y - posN.y;
	if (!horzSpan) dstP = posP.y - posM.y;

	const bool goodSpanN = (lumaEndN < 0.0) != lumaMLTZero;
	const float spanLength = (dstP + dstN);
	const bool goodSpanP = (lumaEndP < 0.0) != lumaMLTZero;
	const float spanLengthRcp = 1.0 / spanLength;

	const bool directionN = dstN < dstP;
	const float dst = min(dstN, dstP);
	const bool goodSpan = directionN ? goodSpanN : goodSpanP;
	const float subpixG = subpixF * subpixF;
	const float pixelOffset = (dst * (-spanLengthRcp)) + 0.5;
	const float subpixH = subpixG * SUBPIX;

	const float pixelOffsetGood = goodSpan ? pixelOffset : 0.0;
	const float pixelOffsetSubpix = max(pixelOffsetGood, subpixH);
	if (!horzSpan) posM.x += pixelOffsetSubpix * lengthSign;
	if (horzSpan) posM.y += pixelOffsetSubpix * lengthSign;
	FragColor = vec4(top(posM).rgb, 1.0);
}
