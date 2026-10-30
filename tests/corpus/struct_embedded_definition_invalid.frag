#version 320 es
precision highp float;
out vec4 color;
struct B{struct A{float x;} a;}; void main(){color=vec4(1.);}
