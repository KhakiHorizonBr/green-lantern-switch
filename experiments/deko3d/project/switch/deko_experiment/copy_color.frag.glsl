#version 460
// SPDX-License-Identifier: GPL-3.0-or-later
layout(binding=0) uniform sampler2D source;
layout(std140,binding=0) uniform Params {ivec2 source_origin;ivec2 dest_origin;uvec2 size;float color_scale;uint padding;};
layout(location=0) out vec4 color;
void main(){ivec2 p=ivec2(gl_FragCoord.xy)-dest_origin;color=texelFetch(source,source_origin+p,0)*color_scale;}
