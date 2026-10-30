#version 320 es
precision highp float;
uniform int radius; // @minmax{0, 8} @init{2}
out vec4 color;
void main(){float a=0.;for(int i=-radius;i<=radius;++i){a+=.1;}color=vec4(a);}
