#version 320 es
precision highp float;
out vec4 color;
void main(){float[2] a=float[2](1.,2.),b=float[2](3.,4.);color=vec4(a[0],a[1],b[0],b[1])/4.;}
