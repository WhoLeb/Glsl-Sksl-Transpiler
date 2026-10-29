#version 320 es
precision highp float;
out vec4 color;
struct S{mat2 m;vec2 v;}; void main(){S s=S(mat2(2.),vec2(1.));color=vec4(s.m*s.v,0.,1.)/2.;}
