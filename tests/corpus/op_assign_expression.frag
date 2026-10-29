#version 320 es
precision highp float;
out vec4 color;
void main(){float a;float b=(a=.5)+1.;color=vec4(a,b,0.,1.);}
