#version 320 es
precision highp float;
out vec4 color;
uniform float u; void main(){float a[2]=float[2](1.,2.);float b[2]=float[2](3.,4.);float c[2];c=u>0.?a:b;color=vec4(c[0]);}
