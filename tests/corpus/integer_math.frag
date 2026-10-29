#version 320 es
precision highp float;
out vec4 color;
void main(){int a=2;a+=3;a*=2;color=vec4(float(a)/10.);}
