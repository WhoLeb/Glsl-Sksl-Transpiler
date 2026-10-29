#version 320 es
precision highp float;
out vec4 color;
void main(){float a=1.,b[2],c=3.;b[0]=a;b[1]=c;color=vec4(b[0],b[1],c,1.)/3.;}
