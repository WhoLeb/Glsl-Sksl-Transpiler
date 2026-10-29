#version 320 es
precision highp float;
out vec4 color;
void main(){int a=0xFF,b=0x0,c=0XaB;color=vec4(float(a+b+c)/1000.);}
