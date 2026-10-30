#version 320 es
precision highp float;
out vec4 color;
#line 40
void main(){color=vec4(float(__LINE__)/100.);}
