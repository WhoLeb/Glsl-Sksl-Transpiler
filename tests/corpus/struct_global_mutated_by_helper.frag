#version 320 es
precision highp float;
out vec4 color;
struct S{float x;};S g;void bump(){g.x+=.25;}void main(){g.x=0.;bump();bump();color=vec4(g.x);}
