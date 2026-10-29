#version 320 es
precision highp float;
#define SQ(x) ((x)*(x))
out vec4 color;
void main(){color=vec4(SQ(.5));}
