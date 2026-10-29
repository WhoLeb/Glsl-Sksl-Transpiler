#version 320 es
precision highp float;
out vec4 color;
void main(){float a=1.,b=a+1.,c=b*2.;color=vec4(c/4.);}
