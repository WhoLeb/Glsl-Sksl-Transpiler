#version 320 es
precision highp float;
out vec4 color;
float f(float length){return length*.5;} void main(){color=vec4(f(1.));}
