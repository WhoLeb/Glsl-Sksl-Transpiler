#version 320 es
precision highp float;
out vec4 color;
float f(float x){return x;}const float c=f(1.);void main(){color=vec4(c);}
