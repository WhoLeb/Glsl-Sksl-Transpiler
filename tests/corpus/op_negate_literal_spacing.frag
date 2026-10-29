#version 320 es
precision highp float;
out vec4 color;
void main(){float a=1.;float b=a- -.5;float c=a-(-.5);float d=a--.5;color=vec4(b,c,d,1.);}
