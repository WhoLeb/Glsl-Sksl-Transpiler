#version 320 es
precision highp float;
out vec4 color;
struct L{float a;float b[2];};bool f(L i,out L o){return false;}void main(){L x=L(1.,float[2](2.,3.));bool b=f(x,x);color=vec4(b?1.:x.a);}
