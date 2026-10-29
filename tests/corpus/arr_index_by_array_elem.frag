#version 320 es
precision highp float;
out vec4 color;
void main(){int idx[2]=int[2](1,0);float a[2]=float[2](.25,.75);color=vec4(a[idx[0]]);}
