#version 320 es
precision highp float;
out vec4 color;
void main(){float t=1.;float r=0.;for(int i=0;i<16&&t<2.;i++){r+=.1;t+=.05;}color=vec4(r);}
