#version 320 es
precision highp float;
uniform int count;uniform int start;uniform int step;out vec4 color;
void main(){float a=0.;for(int i=start;i<8;++i){a+=.1;}color=vec4(a);}
