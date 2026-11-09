#version 320 es
precision highp float;
out vec4 color;
const uint A=0x80000000u;const uint B=A>>31u;const float E=float(A);void main(){color=vec4(float(B),E/4294967296.,0.,1.);}
