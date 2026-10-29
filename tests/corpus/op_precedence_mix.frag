#version 320 es
precision highp float;
out vec4 color;
void main(){float a=1.+2.*3.-4./2.;bool b=1.+1.>1.&&2.<3.||false;float c=(1.+2.)*3.;color=vec4(a,b?1.:0.,c,1.);}
