#version 320 es
precision highp float;
out vec4 color;
float f(float a,float b,float c,float d,float e,float g,float h,float i){return a+b+c+d+e+g+h+i;}void main(){color=vec4(f(.1,.1,.1,.1,.1,.1,.1,.1));}
