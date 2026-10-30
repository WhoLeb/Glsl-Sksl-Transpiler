#version 320 es
precision highp float;
out vec4 color;
#if (1+2)*3 == 9
#if 7/2 == 3 && -1 < 0 && (5 % 3) == 2 && (1 << 3) == 8
#define V .25
#endif
#endif
void main(){color=vec4(V);}
