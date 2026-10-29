#version 320 es
precision highp float;
out vec4 color;
struct float2{float x;}; void main(){float2 v;v.x=.5;color=vec4(v.x);}
