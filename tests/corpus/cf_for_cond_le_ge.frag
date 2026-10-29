#version 320 es
precision highp float;
out vec4 color;
void main(){float a=0.;for(int i=0;i<=3;++i)a+=.1;for(int j=3;j>=0;--j)a+=.1;color=vec4(a);}
