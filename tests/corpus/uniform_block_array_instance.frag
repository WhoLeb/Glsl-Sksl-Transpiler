#version 320 es
precision highp float;
uniform U{float a;} u[2];out vec4 color;
void main(){color=vec4(u[0].a);}
