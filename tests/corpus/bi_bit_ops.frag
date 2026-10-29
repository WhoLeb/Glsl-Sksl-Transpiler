#version 320 es
precision highp float;
out vec4 color;
void main(){color=vec4(float(bitCount(7u)+findLSB(8u)+findMSB(8u))/10.);}
