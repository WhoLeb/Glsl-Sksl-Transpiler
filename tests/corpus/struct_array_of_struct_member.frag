#version 320 es
precision highp float;
out vec4 color;
struct A{float v;}; struct B{A a[2];}; void main(){B b=B(A[2](A(.25),A(.5)));b.a[1].v+=.25;color=vec4(b.a[0].v,b.a[1].v,0.,1.);}
