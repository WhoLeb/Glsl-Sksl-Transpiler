#version 320 es
precision highp float;
out vec4 color;
void main(){float s=0.;for(uint i=0u;i<4u;i++)s+=.25;color=vec4(s);}
