#version 320 es
precision highp float;
out vec4 color;
void main(){color=vec4(float(abs(-2)+sign(-3)+abs(ivec2(-1)).x)/4.);}
