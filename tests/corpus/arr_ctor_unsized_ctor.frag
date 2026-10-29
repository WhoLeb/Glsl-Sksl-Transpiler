#version 320 es
precision highp float;
out vec4 color;
void main(){float a[3]=float[](1.,2.,3.);color=vec4(a[0],a[1],a[2],1.)/3.;}
