#version 320 es
precision highp float;
uniform int count; // @minmax{0, 1000000}
out vec4 color;
void main(){float a=0.;for(int i=0;i<count;++i){a+=.1;}color=vec4(a);}
