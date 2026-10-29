#version 320 es
precision highp float;
out vec4 color;
void main(){color=vec4(float(floatBitsToInt(1.))/1e9);}
