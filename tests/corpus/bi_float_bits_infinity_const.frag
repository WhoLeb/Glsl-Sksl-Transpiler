#version 320 es
precision highp float;
out vec4 color;
const float INF=uintBitsToFloat(0x7F800000u);void main(){color=vec4(INF>1.?1.:0.);}
