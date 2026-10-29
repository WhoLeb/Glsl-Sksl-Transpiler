#version 320 es
precision highp float;
out vec4 color;
void main(){ivec4 v=ivec4(1,2,3,4);color=vec4(v.wzyx)/4.;}
