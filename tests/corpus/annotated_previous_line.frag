#version 320 es
precision highp float;
// @minmax{1.0, 4.0}
uniform float r;
out vec4 color;
void main(){float a=0.;for(float x=0.;x<r*2.;x+=.5){a+=.01;}color=vec4(a);}
