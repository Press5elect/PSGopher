#version 450
// PSGopher: the quick menu's pixels: the texture (font atlas or picture) times the vertex colour.
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional term (GPL-3.0 section 7(b)): keep the attribution "PSGopher by Press5elect"; see NOTICE.
layout(set = 0, binding = 0) uniform sampler2D tex;
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_colour;
layout(location = 0) out vec4 colour;
void main()
{
    colour = v_colour * texture(tex, v_uv);
}
