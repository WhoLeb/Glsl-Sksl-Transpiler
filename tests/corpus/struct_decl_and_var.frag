#version 320 es
precision highp float;
out vec4 color;
struct S{float x;} s=S(.5);void main(){color=vec4(s.x);}
