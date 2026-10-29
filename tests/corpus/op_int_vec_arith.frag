#version 320 es
precision highp float;
out vec4 color;
void main(){ivec2 a=ivec2(1,2)*3+ivec2(1);ivec2 b=a/2-1;color=vec4(vec2(b),0.,1.);}
