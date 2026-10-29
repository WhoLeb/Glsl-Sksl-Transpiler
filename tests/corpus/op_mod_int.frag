#version 320 es
precision highp float;
out vec4 color;
void main(){int a=7%3,b=7/2,c=-7/2;color=vec4(float(a+b+c));}
