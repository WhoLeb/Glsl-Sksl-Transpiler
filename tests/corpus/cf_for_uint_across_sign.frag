#version 320 es
precision highp float;
out vec4 color;
void main(){float s=0.;for(uint i=0x7FFFFFFEu;i<0x80000002u;i++)s+=.25;color=vec4(s);}
