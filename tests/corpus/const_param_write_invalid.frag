#version 320 es
precision highp float;
out vec4 color;
float f(const in float x){x=1.;return x;} void main(){color=vec4(f(1.));}
