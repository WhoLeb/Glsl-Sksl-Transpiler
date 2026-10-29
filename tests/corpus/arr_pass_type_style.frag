#version 320 es
precision highp float;
out vec4 color;
float f(float[3] a){return a[1];} void main(){color=vec4(f(float[3](1.,2.,3.))/3.);}
