#version 320 es
precision highp float;
out vec4 color;
float f(float a[2]){return a[0]+a[1];} void main(){color=vec4(f(float[2](f(float[2](.1,.2)),.3)));}
