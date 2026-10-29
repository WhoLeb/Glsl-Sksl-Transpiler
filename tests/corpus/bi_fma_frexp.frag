#version 320 es
precision highp float;
out vec4 color;
void main(){int e;color=vec4(fma(1.,2.,3.)/5.,frexp(8.,e)/2.,float(e)/4.,1.);}
