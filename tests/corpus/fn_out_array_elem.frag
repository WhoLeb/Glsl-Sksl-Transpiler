#version 320 es
precision highp float;
out vec4 color;
void f(out float x){x=.5;}void main(){float a[2];f(a[1]);a[0]=0.;color=vec4(a[0],a[1],0.,1.);}
