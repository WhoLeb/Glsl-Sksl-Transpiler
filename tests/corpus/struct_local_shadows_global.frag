#version 320 es
precision highp float;
out vec4 color;
struct S{float x;}; void main(){S g=S(.25);{struct S{vec2 y;};S l=S(vec2(.5));color=vec4(g.x,l.y,1.);}}
