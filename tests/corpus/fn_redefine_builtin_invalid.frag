#version 320 es
precision highp float;
out vec4 color;
float sin(float x){return x;} void main(){color=vec4(sin(.5));}
