#version 320 es
precision highp float;
out vec4 color;
void main(){color=vec4(unpackHalf2x16(packHalf2x16(vec2(.5))),0.,1.);}
