#version 320 es
precision highp float;
out vec4 color;
void main(){float x=.5;float r=x>.7?1.:x>.3?.5:0.;color=vec4(r);}
