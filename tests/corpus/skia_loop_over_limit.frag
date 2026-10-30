#version 320 es
precision highp float;
uniform float u;out vec4 color;
void main(){float s=0.;for(int i=0;i<100000;i++){s+=u;if(s>1.)break;}color=vec4(s);}
