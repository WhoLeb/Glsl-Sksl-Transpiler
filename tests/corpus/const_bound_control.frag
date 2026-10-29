#version 320 es
precision highp float;
out vec4 color;
void main(){const int n=4;float a=0.;for(int i=0;i<n;++i){a+=.1;}color=vec4(a);}
