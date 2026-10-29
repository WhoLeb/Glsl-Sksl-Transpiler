#version 320 es
precision highp float;
out vec4 color;
void main(){color=vec4(exp2(2.)/4.,log2(8.)/3.,inversesqrt(4.),pow(2.,.5)/2.);}
