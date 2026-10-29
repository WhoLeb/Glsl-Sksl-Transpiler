#version 320 es
precision highp float;
out vec4 color;
struct S{float x;vec2 y;}; void main(){S s=S(1.);color=vec4(s.x);}
