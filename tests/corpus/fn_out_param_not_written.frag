#version 320 es
precision highp float;
out vec4 color;
void f(out float x){}void main(){float a=.5;f(a);color=vec4(0.);}
