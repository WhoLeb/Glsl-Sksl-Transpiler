#version 320 es
precision highp float;
out vec4 color;
float f(float a,int b){return 1.;}float f(int a,float b){return 2.;} void main(){color=vec4(f(1,1));}
