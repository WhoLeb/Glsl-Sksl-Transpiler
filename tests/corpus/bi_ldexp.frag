#version 320 es
precision highp float;
out vec4 color;
void main(){color=vec4(ldexp(.5,2)/2.);}
