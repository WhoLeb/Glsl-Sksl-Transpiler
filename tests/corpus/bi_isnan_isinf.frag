#version 320 es
precision highp float;
out vec4 color;
void main(){float z=0.;color=vec4(isnan(z/z)?1.:0.,isinf(1./z)?1.:0.,0.,1.);}
