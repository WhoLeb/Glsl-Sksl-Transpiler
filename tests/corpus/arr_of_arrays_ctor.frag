#version 320 es
precision highp float;
out vec4 color;
void main(){float a[2][2]=float[2][2](float[2](1.,2.),float[2](3.,4.));color=vec4(a[1][0]/4.);}
