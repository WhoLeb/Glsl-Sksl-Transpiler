#version 320 es
precision highp float;
out vec4 color;
void main(){mat2 m=mat2(1.,2.,3.,4.);color=vec4(m[0][1],m[1][0],m[0][0],m[1][1])/4.;}
