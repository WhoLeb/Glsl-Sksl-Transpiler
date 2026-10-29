#version 320 es
precision highp float;
out vec4 color;
void f(inout float a[2]){a[1]=7.;} void main(){float x[2]=float[2](1.,2.);f(x);color=vec4(x[1]/7.);}
