#version 320 es
precision highp float;
out vec4 color;
#define A
#if defined(A) && !defined B
#define V .25
#else
#define V .75
#endif
void main(){color=vec4(V);}
