#version 320 es
precision highp float;
out vec4 color;
void main(){color=vec4(length(-2.),dot(2.,3.)/6.,distance(1.,3.)/2.,normalize(-2.));}
