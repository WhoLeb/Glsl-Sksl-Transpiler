#version 320 es
precision highp float;
out vec4 color;
void main(){struct {float v;} a,b;a.v=.25;b.v=.5;color=vec4(a.v,b.v,0.,1.);}
