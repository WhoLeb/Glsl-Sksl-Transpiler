#version 320 es
precision highp float;
out vec4 color;
void main(){int a=int(1.9),b=int(-1.9);color=vec4(float(a+b)+.5);}
