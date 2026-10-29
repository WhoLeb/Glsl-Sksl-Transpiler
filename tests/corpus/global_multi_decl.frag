#version 320 es
precision highp float;
out vec4 color;
float a=1.,b=2.,c;void main(){c=a+b;color=vec4(c/3.);}
