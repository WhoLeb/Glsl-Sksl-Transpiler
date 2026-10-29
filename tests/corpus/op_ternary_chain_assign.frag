#version 320 es
precision highp float;
out vec4 color;
void main(){float a=1.,b=2.;float r=true?a=3.:b=4.;color=vec4(r,a,b,1.);}
