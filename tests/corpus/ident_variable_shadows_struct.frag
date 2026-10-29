#version 320 es
precision highp float;
out vec4 color;
struct S{float x;}; void main(){S s;s.x=1.;{float S=2.;color=vec4(S+s.x);}}
