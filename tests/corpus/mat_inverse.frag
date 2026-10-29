#version 320 es
precision highp float;
out vec4 color;
void main(){mat2 m=inverse(mat2(2.,0.,0.,4.));color=vec4(m[0],m[1]);}
