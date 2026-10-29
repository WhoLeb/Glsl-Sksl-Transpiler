#version 320 es
precision highp float;
out vec4 color;
void main(){float a=7.%3.;color=vec4(a);}
