#version 320 es
precision highp float;
out vec4 color;
struct A{vec3 v;};struct B{A a;}; void main(){B b;b.a.v=vec3(0.);b.a.v.y=.5;b.a.v.zx=vec2(.25);color=vec4(b.a.v,1.);}
