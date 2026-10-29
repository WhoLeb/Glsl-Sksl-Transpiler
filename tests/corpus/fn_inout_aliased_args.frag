#version 320 es
precision highp float;
out vec4 color;
void f(inout float a,inout float b){a=1.;b=2.;}void main(){float x=0.;f(x,x);color=vec4(x/2.);}
