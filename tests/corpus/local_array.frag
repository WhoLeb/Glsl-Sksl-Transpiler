#version 320 es
precision highp float;
out vec4 color;
void main(){float a[2];a[0]=.2;a[1]=.4;color=vec4(a[0]+a[1]);}
