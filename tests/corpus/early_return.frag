#version 320 es
precision highp float;
out vec4 color;
void main(){color=vec4(.5);if(true)return;color=vec4(1.);}
