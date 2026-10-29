#version 320 es
precision highp float;
out vec4 color;
void main(){float a[2]=float[2](1.,2.);float b[2]=float[2](1.,2.);color=vec4(a==b?1.:0.);}
