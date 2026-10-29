#version 320 es
precision highp float;
uniform int count;uniform int start;uniform int step;out vec4 color;
void main(){float a[3];a[0]=.1;a[1]=.2;a[2]=.3;color=vec4(a[clamp(count,0,2)]);}
