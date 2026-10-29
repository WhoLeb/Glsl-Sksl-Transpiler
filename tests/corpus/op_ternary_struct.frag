#version 320 es
precision highp float;
out vec4 color;
struct S{float x;}; void main(){S a,b;a.x=1.;b.x=2.;S c=true?a:b;color=vec4(c.x);}
