#version 320 es
precision highp float;
out vec4 color;
void main(){float a[2]=float[2](1.,2.);float b[2];float x=(b=a)[0];color=vec4(x);}
