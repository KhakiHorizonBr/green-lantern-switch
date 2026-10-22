#version 450
// SPDX-License-Identifier: GPL-3.0-or-later
// The known Green Lantern depth-restore pass writes depth via oDepth and the
// saved stencil via oC0.r. Host color and depth are separate Vulkan images.
// Pack R8 bytes from an RGBA8 transfer into a tightly packed stencil transfer.
// Color and depth/stencil swap the two 40-sample halves of each 80-sample
// EDRAM tile (VulkanRenderTargetCache transfer shader). The guest shader remaps
// its source reads; its color output still has the color EDRAM address layout.
// Flag1 applies that final color -> stencil permutation for aligned 1x regions.
layout(local_size_x = 64) in;
layout(set = 0, binding = 0) buffer Data { uint words[]; };
layout(push_constant) uniform Params {
  uint count;
  uint row_words;
  uint unused;
  uint packed_offset;
  uint flags;
};
void main() {
  uint out_index = gl_GlobalInvocationID.x;
  uint first = out_index * 4u;
  if (first >= count) return;
  uint value = 0u;
  for (uint channel = 0u; channel < 4u; ++channel) {
    uint pixel = first + channel;
    if (pixel < count) {
      uint source = pixel;
      if ((flags & 1u) != 0u) {
        uint x = pixel % row_words;
        uint source_x = (x / 80u) * 80u + ((x % 80u + 40u) % 80u);
        source = pixel - x + source_x;
      }
      value |= (words[source] & 255u) << (channel * 8u);
    }
  }
  words[packed_offset + out_index] = value;
}
