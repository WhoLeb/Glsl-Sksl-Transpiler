#version 320 es
precision highp float;
out vec4 color;
float f(float x){{float x=2.;return x;}} void main(){color=vec4(f(1.)/2.);}
