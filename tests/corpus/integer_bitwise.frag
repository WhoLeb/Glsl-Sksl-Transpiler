#version 320 es
precision highp float;
uniform int count;uniform int start;uniform int step;out vec4 color;
void main(){int a=(count & 7) ^ 2;a=(a<<1)%7;color=vec4(float(a));}
