#version 320 es
precision highp float;
out vec4 color;
#define ADD(a,b) ((a)+(b))
void main(){color=vec4(ADD(vec2(.1,.2).x,ADD(.1,max(.1,.2))));}
