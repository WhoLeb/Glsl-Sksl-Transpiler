#version 320 es
precision highp float;
out vec4 color;
void main(){color=vec4(float(bitfieldInsert(0,3,2,2)+bitfieldReverse(1))/16.);}
