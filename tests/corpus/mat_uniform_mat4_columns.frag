#version 320 es
precision highp float;
uniform mat4 m;out vec4 color;
void main(){color=m[3];}
