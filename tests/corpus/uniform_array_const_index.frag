#version 320 es
precision highp float;
uniform vec4 u[4];out vec4 color;
void main(){color=u[2]+u[0];}
