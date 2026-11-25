#version 450
// PSGopher: the in-game quick menu's triangles (Dear ImGui's vertices: position in pixels, UV,
// colour), drawn by parallel-rdp's interface over the frame (ps5/host/quickmenu.cpp).
// SPDX-License-Identifier: GPL-3.0-or-later
layout(location = 0) in vec2 position;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec4 colour;
layout(push_constant) uniform Screen { vec2 scale; vec2 translate; } screen;
layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_colour;
void main()
{
    v_uv = uv;
    v_colour = colour;
    gl_Position = vec4(position * screen.scale + screen.translate, 0.0, 1.0);
}
