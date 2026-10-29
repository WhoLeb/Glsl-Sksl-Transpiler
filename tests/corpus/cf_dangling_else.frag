#version 320 es
precision highp float;
out vec4 color;
void main(){float a=0.;if(true)if(false)a=1.;else a=2.;color=vec4(a/2.);}
