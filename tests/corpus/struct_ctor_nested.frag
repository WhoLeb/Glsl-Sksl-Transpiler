#version 320 es
precision highp float;
out vec4 color;
struct A{float x;};struct B{A a;vec2 v;}; void main(){B b=B(A(.5),vec2(.25));color=vec4(b.a.x,b.v,1.);}
