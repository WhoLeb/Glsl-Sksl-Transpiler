#version 320 es
precision highp float;
uniform uint u;out vec4 color;
void main(){color=vec4(float(u));}
