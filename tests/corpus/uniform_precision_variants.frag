#version 320 es
precision highp float;
uniform lowp vec4 a;uniform mediump float b;uniform highp float c;out vec4 color;
void main(){color=vec4(a.xyz,b+c);}
