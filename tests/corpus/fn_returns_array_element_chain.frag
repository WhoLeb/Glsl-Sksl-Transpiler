#version 320 es
precision highp float;
out vec4 color;
void main(){float a[2]=float[2](.25,.5);color=vec4(a[int(a[0]*4.)]);}
