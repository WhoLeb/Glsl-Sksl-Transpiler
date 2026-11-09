#version 320 es
precision highp float;
out vec4 color;
const uint A=floatBitsToUint(1.0);const float B=uintBitsToFloat(0x3FC00000u);void main(){color=vec4(float(A>>23u)/255.,B/2.,0.,1.);}
