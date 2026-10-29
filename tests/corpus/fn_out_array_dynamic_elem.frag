#version 320 es
precision highp float;
out vec4 color;
void f(out float x){x=.5;}void main(){float a[2];for(int i=0;i<2;++i)f(a[i]);color=vec4(a[0],a[1],0.,1.);}
