#version 320 es
precision highp float;
struct S{sampler2D t;}; uniform S s;out vec4 color;
void main(){color=vec4(1.);}
