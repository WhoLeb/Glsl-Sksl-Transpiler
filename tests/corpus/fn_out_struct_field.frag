#version 320 es
precision highp float;
out vec4 color;
struct S{float x;};void f(out float x){x=.5;}void main(){S s;f(s.x);color=vec4(s.x);}
