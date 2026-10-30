#version 320 es
precision highp float;
out vec4 color;
void main(){mat2 m=mat2(vec4(1.,2.,3.,4.));color=vec4(m[0],m[1])/4.;}
