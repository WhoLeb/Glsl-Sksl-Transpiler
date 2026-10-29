#version 320 es
precision highp float;
out vec4 color;
struct S{float x;}; void main(){S a=S(1.);S b=a;b.x=2.;color=vec4(a.x,b.x,0.,1.)/2.;}
