#version 320 es
precision highp float;
layout(std430,binding=0) buffer Values {float value;};out vec4 color;
void main(){color=vec4(value);}
