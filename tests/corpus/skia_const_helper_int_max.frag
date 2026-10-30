#version 320 es
precision highp float;
out vec4 color;
void main(){const int a=5;const int b=max(a-2,0);color=vec4(float(b));}
