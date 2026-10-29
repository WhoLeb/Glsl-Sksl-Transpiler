#version 320 es
precision highp float;
out vec4 color;
void f(in float x,out float y,inout float z){y=x;z+=x;}void main(){float y;float z=0.;f(1.,y,z);color=vec4(y+z);}
