#version 320 es
precision highp float;
uniform bool b;out vec4 color;
void main(){color=b?vec4(1.):vec4(0.);}
