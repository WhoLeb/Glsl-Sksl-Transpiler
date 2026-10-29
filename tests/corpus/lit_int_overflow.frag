#version 320 es
precision highp float;
out vec4 color;
void main(){int a=2147483648;color=vec4(float(a));}
