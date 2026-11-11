#version 460
layout(binding=0) uniform sampler2D source_image;
layout(std140,binding=0) uniform Gamma { vec4 ramp[256]; } gamma;
layout(location=0) in vec2 uv;
layout(location=0) out vec4 color;
void main() {
  vec3 sample_color=clamp(textureLod(source_image,uv,0.0).rgb,0.0,1.0);
  ivec3 index=ivec3(sample_color*255.0+0.5);
  color=vec4(gamma.ramp[index.r].r,gamma.ramp[index.g].g,gamma.ramp[index.b].b,1.0);
}
