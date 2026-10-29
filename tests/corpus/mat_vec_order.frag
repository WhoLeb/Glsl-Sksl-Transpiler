#version 320 es
precision highp float;
out vec4 color;
void main(){mat2 m=mat2(1.,2.,3.,4.);vec2 v=vec2(1.,1.);vec2 a=m*v;vec2 b=v*m;color=vec4(a,b)/7.;}
