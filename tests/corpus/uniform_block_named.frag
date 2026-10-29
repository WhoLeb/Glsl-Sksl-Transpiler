#version 320 es
precision highp float;
uniform U{vec3 a;float b;} u;out vec4 color;
void main(){color=vec4(u.a,u.b);}
