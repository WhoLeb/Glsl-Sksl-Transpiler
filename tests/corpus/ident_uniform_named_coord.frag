#version 320 es
precision highp float;
uniform vec2 coord;out vec4 color;
void main(){color=vec4(coord,0.,1.);}
