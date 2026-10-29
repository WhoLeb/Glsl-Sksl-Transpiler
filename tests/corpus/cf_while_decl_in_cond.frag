#version 320 es
precision highp float;
out vec4 color;
void main(){float a=0.;int i=0;while(bool b=i<3){a+=.1;++i;}color=vec4(a);}
