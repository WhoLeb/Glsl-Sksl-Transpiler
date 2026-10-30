#version 320 es
precision highp float;
out vec4 color;
float f(int n){float s=0.;for(int i=0;i<n;i++)s+=.1;return s;}void main(){color=vec4(f(3),f(5),0.,1.);}
