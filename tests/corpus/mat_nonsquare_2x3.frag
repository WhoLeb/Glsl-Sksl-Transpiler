#version 320 es
precision highp float;
out vec4 color;
void main(){mat2x3 m=mat2x3(1.);color=vec4(m[1],1.);}
