#version 320 es
precision highp float;
out vec4 color;
void main(){int i;for(i=0;i<3;++i);color=vec4(float(i)/3.);}
