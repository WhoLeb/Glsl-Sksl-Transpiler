#version 320 es
precision highp float;
out vec4 color;
void main(){const float f=2./tanh(1.);color=vec4(f);}
