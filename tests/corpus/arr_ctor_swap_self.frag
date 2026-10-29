#version 320 es
precision highp float;
out vec4 color;
void main(){float a[2]=float[2](1.,2.);a=float[2](a[1],a[0]);color=vec4(a[0],a[1],0.,1.)/2.;}
