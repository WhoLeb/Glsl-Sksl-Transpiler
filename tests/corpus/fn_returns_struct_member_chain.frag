#version 320 es
precision highp float;
out vec4 color;
struct S{vec2 v;};S make(){return S(vec2(.25,.5));}void main(){color=vec4(make().v.yx,0.,1.);}
