#version 320 es
precision highp float;
out vec4 color;
void main(){mat2 m=matrixCompMult(mat2(1.,2.,3.,4.),mat2(2.));color=vec4(m[0],m[1])/8.;}
