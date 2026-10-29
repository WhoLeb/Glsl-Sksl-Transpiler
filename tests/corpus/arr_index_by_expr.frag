#version 320 es
precision highp float;
out vec4 color;
void main(){float a[4]=float[4](1.,2.,3.,4.);int i=1;color=vec4(a[i+1]/4.);}
