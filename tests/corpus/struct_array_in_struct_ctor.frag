#version 320 es
precision highp float;
out vec4 color;
struct S{float a[2];}; void main(){S s=S(float[2](.25,.5));color=vec4(s.a[0],s.a[1],0.,1.);}
