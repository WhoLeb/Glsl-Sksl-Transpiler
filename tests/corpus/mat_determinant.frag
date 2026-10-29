#version 320 es
precision highp float;
out vec4 color;
void main(){color=vec4(determinant(mat2(1.,2.,3.,4.)),determinant(mat3(2.)),0.,1.);}
