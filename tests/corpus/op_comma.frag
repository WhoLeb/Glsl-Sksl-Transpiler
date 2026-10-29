#version 320 es
precision highp float;
out vec4 color;
void main(){int a=1,b=2;int c=(a++,b++,a+b);color=vec4(float(c));}
