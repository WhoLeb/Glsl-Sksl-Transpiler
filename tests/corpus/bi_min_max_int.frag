#version 320 es
precision highp float;
out vec4 color;
void main(){color=vec4(float(min(2,3)+max(ivec2(1),ivec2(2)).x)/5.);}
