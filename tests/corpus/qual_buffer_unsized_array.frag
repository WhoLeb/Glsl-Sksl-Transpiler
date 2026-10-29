#version 320 es
precision highp float;
layout(std430,binding=1) buffer B{float d[];};out vec4 color;
void main(){color=vec4(d[0]);}
