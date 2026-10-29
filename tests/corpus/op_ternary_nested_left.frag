#version 320 es
precision highp float;
out vec4 color;
void main(){float x=.5;float r=x>.1?x>.4?1.:2.:3.;color=vec4(r);}
