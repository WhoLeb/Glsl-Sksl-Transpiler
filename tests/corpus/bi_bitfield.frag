#version 320 es
precision highp float;
out vec4 color;
void main(){color=vec4(float(bitfieldExtract(255,0,4))/15.);}
