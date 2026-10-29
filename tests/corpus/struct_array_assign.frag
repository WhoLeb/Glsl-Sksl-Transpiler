#version 320 es
precision highp float;
out vec4 color;
struct S{float a[2];float b;}; void main(){S s=S(float[2](1.,2.),3.);S t;t=s;color=vec4(t.a[1],t.b,0.,1.)/3.;}
