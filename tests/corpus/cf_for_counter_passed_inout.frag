#version 320 es
precision highp float;
out vec4 color;
void skip(inout int i){i+=1;} void main(){float a=0.;for(int i=0;i<8;++i){a+=.1;skip(i);}color=vec4(a);}
