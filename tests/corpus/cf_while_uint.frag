#version 320 es
precision highp float;
out vec4 color;
void main(){uint i=0u;float s=0.;while(i<4u){i++;s+=.25;}color=vec4(s);}
