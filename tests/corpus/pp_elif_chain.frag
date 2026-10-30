#version 320 es
precision highp float;
out vec4 color;
#define MODE 2
#if MODE == 1
#define V .1
#elif MODE == 2
#define V .2
#else
#define V .3
#endif
void main(){color=vec4(V);}
