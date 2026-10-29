#version 320 es
precision highp float;
out vec4 color;
void main(){color=vec4(float[3](.1,.2,.3)[1]);}
