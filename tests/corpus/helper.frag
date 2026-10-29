#version 320 es
precision highp float;
out vec4 color;
float scale(float x){return x*.5;} void main(){color=vec4(scale(1.));}
