#version 320 es
precision highp float;
out vec4 color;
const int I[3]=int[3](2,1,0);void main(){color=vec4(float(I[0])/2.);}
