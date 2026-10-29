#version 320 es
precision highp float;
out vec4 color;
void main(){color=vec4(fract(-.25),fract(-1.75),fract(1.25),1.);}
