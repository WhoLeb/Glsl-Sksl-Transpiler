#version 320 es
precision highp float;
out vec4 color;
struct S{float a[2];}; void main(){S s=S(float[2](1.,2.));s=S(float[2](s.a[1],s.a[0]));color=vec4(s.a[0]/2.);}
