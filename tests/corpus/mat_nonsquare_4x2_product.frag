#version 320 es
precision highp float;
out vec4 color;
void main(){mat4x2 a=mat4x2(1.);mat2x4 b=mat2x4(1.);mat2 c=a*b;color=vec4(c[0],c[1]);}
