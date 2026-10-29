#version 320 es
precision highp float;
out vec4 color;
float side(inout float a){a+=1.;return a;} void main(){float a=0.;float r=a>0.?side(a):-side(a);color=vec4(r,a,0.,1.);}
