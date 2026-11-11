#version 460
// SPDX-License-Identifier: GPL-3.0-or-later
// A bit-plane pass writes only pixels whose saved stencil contains this bit.
layout(binding=0) uniform sampler2D source;
layout(std140,binding=0) uniform Params {ivec2 origin;uvec2 size;uint bit;uint swap_halves;uvec2 padding;};
void main(){ivec2 p=ivec2(gl_FragCoord.xy);if(swap_halves!=0u)p.x=(p.x/80)*80+(p.x%80+40)%80;
 uint value=uint(round(clamp(texelFetch(source,p,0).r,0.0,1.0)*255.0));if((value&bit)==0u)discard;}
