#version 320 es
precision highp float;
out vec4 color;
void main(){gl_SampleMask[0]=1;color=vec4(1.);}
