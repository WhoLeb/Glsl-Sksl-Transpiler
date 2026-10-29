#version 320 es
precision highp float;
uniform vec4 u[4];uniform int count;out vec4 color;
void main(){color=u[count];}
