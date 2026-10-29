#version 320 es
precision highp float;
out vec4 color;
void main(){int a=010,b=0,c=007;color=vec4(float(a+b+c)/100.);}
