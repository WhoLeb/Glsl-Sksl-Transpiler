#version 320 es
precision highp float;
out vec4 color;
struct {float x;vec2 y;} s; void main(){s.x=.5;s.y=vec2(.25);color=vec4(s.x,s.y,1.);}
