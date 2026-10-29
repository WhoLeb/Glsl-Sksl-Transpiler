#version 320 es
precision highp float;
out vec4 color;
void kill(){discard;} void main(){kill();color=vec4(1.);}
