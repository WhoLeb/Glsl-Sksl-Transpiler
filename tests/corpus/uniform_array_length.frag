#version 320 es
precision highp float;
uniform float u[4];out vec4 color;
void main(){color=vec4(float(u.length()));}
