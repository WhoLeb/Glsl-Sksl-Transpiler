#version 320 es
precision highp float;
#pragma something_unknown(1,2)
out vec4 color;
void main(){color=vec4(1.);}
