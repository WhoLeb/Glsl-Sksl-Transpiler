#version 320 es
precision highp float;
out vec4 color;
void main(){color=vec4(asinh(.5),acosh(1.5),atanh(.5),1.);}
