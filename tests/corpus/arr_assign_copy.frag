#version 320 es
precision highp float;
out vec4 color;
void main(){float a[2]=float[2](1.,2.);float b[2];b=a;b[0]=5.;color=vec4(a[0],b[0],0.,1.)/5.;}
