#version 320 es
precision highp float;
out vec4 color;
void main(){mat2 a=mat2(2.,4.,6.,8.);mat2 b=mat2(2.,2.,2.,2.);mat2 c=a/b;mat2 d=8./a;color=vec4(c[1],d[0])/4.;}
