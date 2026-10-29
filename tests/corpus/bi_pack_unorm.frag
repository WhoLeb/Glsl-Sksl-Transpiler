#version 320 es
precision highp float;
out vec4 color;
void main(){color=unpackUnorm4x8(packUnorm4x8(vec4(.5)));}
