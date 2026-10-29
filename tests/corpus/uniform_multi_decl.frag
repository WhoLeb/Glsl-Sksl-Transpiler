#version 320 es
precision highp float;
uniform float a,b,c;out vec4 color;
void main(){color=vec4(a,b,c,1.);}
