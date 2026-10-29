#version 320 es
precision highp float;
out vec4 color;
struct S{float a[2];vec3 v;}; void main(){S s;s.a[0]=.1;s.a[1]=.2;s.v=vec3(.3);color=vec4(s.a[0]+s.a[1],s.v.xy,1.);}
