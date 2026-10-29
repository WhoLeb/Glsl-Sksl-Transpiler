#version 320 es
precision highp float;
out vec4 color;
struct S{float a[2];}; S g(){S s=S(float[2](1.,2.));return s;} void main(){S t=g();color=vec4(t.a[0]);}
