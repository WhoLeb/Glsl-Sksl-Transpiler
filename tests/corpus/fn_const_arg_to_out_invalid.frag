#version 320 es
precision highp float;
out vec4 color;
void f(out float x){x=1.;}void main(){f(1.);color=vec4(1.);}
