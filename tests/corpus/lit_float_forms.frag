#version 320 es
precision highp float;
out vec4 color;
void main(){float a=1.,b=.5,c=1.e2,d=1E+2,e=5e-1,f=0.5e-1,g=1e1,h=00.5;color=vec4(a+b+c+d,e+f,g,h);}
