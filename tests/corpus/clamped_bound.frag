#version 320 es
precision highp float;
uniform int count;out vec4 color;
void main(){float a=0.;for(int i=0;i<min(count,6);++i){a+=.1;}color=vec4(a);}
