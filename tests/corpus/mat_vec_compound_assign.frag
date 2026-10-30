#version 320 es
precision highp float;
out vec4 color;
void main(){vec2 v=vec2(1.,1.);v*=mat2(1.,2.,3.,4.);color=vec4(v/7.,0.,1.);}
