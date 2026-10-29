#version 320 es
precision highp float;
out vec4 color;
void main(){float x=.5;{float y=x;float x=y+.25;color=vec4(x);}}
