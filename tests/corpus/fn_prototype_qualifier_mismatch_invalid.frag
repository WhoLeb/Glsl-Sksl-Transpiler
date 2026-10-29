#version 320 es
precision highp float;
out vec4 color;
void f(in float x);void f(inout float x){x=1.;}void main(){color=vec4(1.);}
