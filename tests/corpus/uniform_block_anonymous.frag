#version 320 es
precision highp float;
layout(std140) uniform U{vec3 a;float b;};out vec4 color;
void main(){color=vec4(a,b);}
