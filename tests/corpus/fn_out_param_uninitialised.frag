#version 320 es
precision highp float;
out vec4 color;
void f(out float x){x=.5;}void main(){float a;f(a);color=vec4(a);}
