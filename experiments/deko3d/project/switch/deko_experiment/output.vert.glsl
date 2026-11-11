#version 460
layout(location=0) out vec2 uv;
void main() {
  vec2 t=vec2(float(gl_VertexID&1),float((gl_VertexID>>1)&1));
  uv=t;gl_Position=vec4(t*2.0-1.0,0.0,1.0);
}
