#version 320 es
precision highp float;
out vec4 color;
void f(inout vec2 a){a=a.yx;} void main(){vec4 v=vec4(1.,2.,3.,4.);f(v.wz);color=v/4.;}
