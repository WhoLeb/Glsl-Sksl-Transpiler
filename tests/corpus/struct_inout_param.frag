#version 320 es
precision highp float;
out vec4 color;
struct S{float x;}; void bump(inout S s){s.x+=.25;} void main(){S s=S(.25);bump(s);bump(s);color=vec4(s.x);}
