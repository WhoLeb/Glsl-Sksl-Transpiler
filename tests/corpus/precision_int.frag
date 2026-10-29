#version 320 es
precision highp float;
out vec4 color;
void main(){lowp int a=1;mediump int b=2;highp int c=3;color=vec4(float(a+b+c)/6.);}
