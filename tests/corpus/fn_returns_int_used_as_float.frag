#version 320 es
precision highp float;
out vec4 color;
int f(){return 2;}void main(){color=vec4(float(f())/2.);}
