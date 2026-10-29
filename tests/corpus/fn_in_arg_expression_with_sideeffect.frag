#version 320 es
precision highp float;
out vec4 color;
float f(float x){return x;}void main(){float a=.25;float b=f(a++);color=vec4(a,b,0.,1.);}
