#version 320 es
precision highp float;
out vec4 color;
void f(inout float x){if(x>.5)return;x=1.;}void main(){float a=.25,b=.75;f(a);f(b);color=vec4(a,b,0.,1.);}
