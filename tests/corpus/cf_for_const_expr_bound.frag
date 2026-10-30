#version 320 es
precision highp float;
out vec4 color;
void main(){float a=0.;const int n=3;for(int i=0;i<n*2-1;++i)a+=.1;color=vec4(a);}
