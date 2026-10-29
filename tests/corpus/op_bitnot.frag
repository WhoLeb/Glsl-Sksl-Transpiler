#version 320 es
precision highp float;
out vec4 color;
void main(){int a=~~5,b=-~5,c=~-5;color=vec4(float(a+b+c));}
