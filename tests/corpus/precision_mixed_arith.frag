#version 320 es
precision highp float;
out vec4 color;
void main(){mediump float a=.5;highp float b=2.;lowp float c=.25;color=vec4(a*b+c);}
