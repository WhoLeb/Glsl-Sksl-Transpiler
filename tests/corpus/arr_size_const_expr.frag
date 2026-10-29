#version 320 es
precision highp float;
out vec4 color;
const int N=2*2;void main(){float a[N+1];a[4]=.5;color=vec4(a[4]);}
