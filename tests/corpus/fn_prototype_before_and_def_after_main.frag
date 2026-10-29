#version 320 es
precision highp float;
out vec4 color;
float f(float x);void main(){color=vec4(f(.5));}float f(float x){return x;}
