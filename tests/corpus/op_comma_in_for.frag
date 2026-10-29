#version 320 es
precision highp float;
out vec4 color;
void main(){int s=0;for(int i=0;i<3;i++,s++){}color=vec4(float(s));}
