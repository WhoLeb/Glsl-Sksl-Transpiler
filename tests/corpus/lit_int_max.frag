#version 320 es
precision highp float;
out vec4 color;
void main(){int a=2147483647;color=vec4(float(a)/2147483647.);}
