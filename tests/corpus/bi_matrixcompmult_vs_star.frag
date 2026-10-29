#version 320 es
precision highp float;
out vec4 color;
void main(){mat2 a=mat2(1.,2.,3.,4.);color=vec4(matrixCompMult(a,a)[1],(a*a)[1])/16.;}
