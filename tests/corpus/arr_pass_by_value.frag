#version 320 es
precision highp float;
out vec4 color;
float f(float a[3]){a[0]=9.;return a[0];} void main(){float x[3]=float[3](1.,2.,3.);float r=f(x);color=vec4(r,x[0],0.,1.)/9.;}
