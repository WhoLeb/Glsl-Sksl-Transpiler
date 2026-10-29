#version 320 es
precision highp float;
layout(binding=0,std140) uniform U{vec4 a;};out vec4 color;
void main(){color=vec4(a);}
