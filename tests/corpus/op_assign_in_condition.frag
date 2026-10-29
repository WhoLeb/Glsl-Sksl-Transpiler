#version 320 es
precision highp float;
out vec4 color;
void main(){float a=0.;if((a=.5)>.25)color=vec4(a);else color=vec4(0.);}
