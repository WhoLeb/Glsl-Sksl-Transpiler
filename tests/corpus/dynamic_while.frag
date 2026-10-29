#version 320 es
precision highp float;
uniform int count;uniform int start;uniform int step;out vec4 color;
void main(){float a=0.;int i=0;while(i<count){a+=.1;++i;}color=vec4(a);}
