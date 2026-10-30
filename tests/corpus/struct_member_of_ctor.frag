#version 320 es
precision highp float;
out vec4 color;
struct S{float x;vec2 y;}; void main(){color=vec4(S(.25,vec2(.5,.75)).y,S(1.,vec2(0.)).x,1.);}
