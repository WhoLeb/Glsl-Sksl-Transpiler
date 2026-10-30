#version 320 es
precision highp float;
out vec4 color;
#define CAT(a,b) a##b
void main(){float xy=.5;color=vec4(CAT(x,y));}
