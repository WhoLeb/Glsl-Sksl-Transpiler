#version 320 es
precision highp float;
out vec4 color;
void main(){color=vec4(float(clamp(5,0,3))/3.);}
