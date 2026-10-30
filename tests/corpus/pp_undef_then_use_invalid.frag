#version 320 es
precision highp float;
out vec4 color;
#define V .25
#undef V
void main(){color=vec4(V);}
