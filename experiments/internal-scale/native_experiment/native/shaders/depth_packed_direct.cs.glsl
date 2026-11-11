#version 450
// SPDX-License-Identifier: GPL-3.0-or-later
// Same float20e4 and packed-byte outputs as depth_resolve.cs.glsl, without
// exporting depth/stencil to a scratch buffer and copying both results back.
#extension GL_EXT_samplerless_texture_functions : require
layout(local_size_x=8, local_size_y=8) in;
layout(set=0,binding=0) uniform texture2D depth_source;
layout(set=0,binding=1) uniform utexture2D stencil_source;
layout(set=0,binding=2,r32f) uniform writeonly image2D depth_dest;
layout(set=0,binding=3,rgba8) uniform writeonly image2D words_dest;
layout(push_constant) uniform Params { ivec2 origin; uvec2 size; };
// ReXGlue/Xenia Float32To20e4 encoding (BSD-3-Clause contributors), as in
// depth_resolve.cs.glsl; see the project's LICENSES directory.
uint encode20e4(float value) {
  if (!(value > 0.0)) return 0u;
  uint bits = floatBitsToUint(value);
  if (bits >= 0x3FFFFFF8u) return 0xFFFFFFu;
  if (bits < 0x38800000u) {
    uint shift = min(113u - (bits >> 23u), 24u);
    bits = (0x800000u | (bits & 0x7FFFFFu)) >> shift;
  } else {
    bits += 0xC8000000u;
  }
  bits += 3u + ((bits >> 3u) & 1u);
  return (bits >> 3u) & 0xFFFFFFu;
}
void main() {
  uvec2 p = gl_GlobalInvocationID.xy;
  if (any(greaterThanEqual(p,size))) return;
  ivec2 c = origin + ivec2(p);
  float z = clamp(texelFetch(depth_source,c,0).r,0.0,1.0);
  uint s = texelFetch(stencil_source,c,0).r & 255u;
  uint packed = (encode20e4(z)<<8u) | s;
  imageStore(depth_dest,c,vec4(z));
  imageStore(words_dest,c,vec4(uvec4(packed,packed>>8u,packed>>16u,packed>>24u)&255u)/255.0);
}
