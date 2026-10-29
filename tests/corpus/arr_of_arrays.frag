#version 320 es
precision highp float;
out vec4 color;
void main(){float a[2][3];a[1][2]=.5;color=vec4(a[1][2]);}
