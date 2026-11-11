#version 450
layout(location=0) out vec4 color;
void main() {
  vec4 position=gl_FragCoord;
  color=vec4(position.xy*0.01,gl_FragCoord.x*0.01,gl_FragCoord.y*0.01);
}
