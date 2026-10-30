#version 320 es
precision highp float;
out vec4 color;
void main(){int i=0;float a=0.;while(i<10){a+=.1;i++;}color=vec4(a);}
