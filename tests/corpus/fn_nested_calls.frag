#version 320 es
precision highp float;
out vec4 color;
float f(float x){return x*.5;}void main(){color=vec4(f(f(f(f(1.)))));}
