#version 320 es
precision highp float;
out vec4 color;
uniform int u;float f(int n){float s=0.;for(int i=0;i<n;i++)s+=.1;return s;}void main(){color=vec4(f(u));}
