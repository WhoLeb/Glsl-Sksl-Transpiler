#version 320 es
precision highp float;
uniform int count;uniform int start;uniform int step;out vec4 color;
void main(){float a=0.;for(int i=0;i<8;i+=step){a+=.1;}color=vec4(a);}
