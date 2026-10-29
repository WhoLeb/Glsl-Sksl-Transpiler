#version 320 es
precision highp float;
out vec4 color;
void main(){ivec3 a=ivec3(7,8,9)%ivec3(2,3,4);color=vec4(vec3(a)/4.,1.);}
