#version 320 es
precision highp float;
out vec4 color;
float inf=uintBitsToFloat(0x7F800000u);void main(){color=vec4(inf>1.?1.:0.);}
