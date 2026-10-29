#version 320 es
precision highp float;
out vec4 color;
void main(){float a,b;(true?a:b)=1.;color=vec4(a);}
