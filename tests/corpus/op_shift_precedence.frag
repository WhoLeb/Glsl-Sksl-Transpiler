#version 320 es
precision highp float;
out vec4 color;
void main(){int a=1<<2+1,b=8>>1-1,c=1<<1<<1;color=vec4(float(a+b+c)/100.);}
